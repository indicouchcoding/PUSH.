const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

function peImage(file) {
  const bytes = fs.readFileSync(file);
  const pe = bytes.readUInt32LE(0x3c);
  if (bytes.toString("ascii", pe, pe + 4) !== "PE\0\0") throw new Error(`${file} is not a PE image`);
  const sectionCount = bytes.readUInt16LE(pe + 6);
  const optionalSize = bytes.readUInt16LE(pe + 20);
  const optional = pe + 24;
  const optionalMagic = bytes.readUInt16LE(optional);
  if (optionalMagic !== 0x20b) throw new Error(`${file} is not a PE32+ image`);
  const dataDirectories = optional + 112;
  const exceptionRva = bytes.readUInt32LE(dataDirectories + 3 * 8);
  const exceptionSize = bytes.readUInt32LE(dataDirectories + 3 * 8 + 4);
  const sections = [];
  let at = pe + 24 + optionalSize;
  for (let index = 0; index < sectionCount; ++index, at += 40) {
    sections.push({
      name: bytes.toString("ascii", at, at + 8).replace(/\0+$/, ""),
      virtualSize: bytes.readUInt32LE(at + 8),
      rva: bytes.readUInt32LE(at + 12),
      rawSize: bytes.readUInt32LE(at + 16),
      raw: bytes.readUInt32LE(at + 20),
    });
  }
  const rvaToOffset = (rva) => {
    const section = sections.find((item) => rva >= item.rva && rva < item.rva + Math.max(item.virtualSize, item.rawSize));
    if (!section || rva - section.rva >= section.rawSize) return -1;
    return section.raw + rva - section.rva;
  };
  const offsetToRva = (offset) => {
    const section = sections.find((item) => offset >= item.raw && offset < item.raw + item.rawSize);
    return section ? section.rva + offset - section.raw : -1;
  };
  const functions = [];
  const exceptionOffset = rvaToOffset(exceptionRva);
  if (exceptionOffset >= 0 && exceptionSize >= 12) {
    for (let offset = exceptionOffset; offset + 12 <= exceptionOffset + exceptionSize; offset += 12) {
      const begin = bytes.readUInt32LE(offset);
      const end = bytes.readUInt32LE(offset + 4);
      if (begin > 0 && end > begin && rvaToOffset(begin) >= 0 && rvaToOffset(end - 1) >= 0)
        functions.push({begin, end});
    }
    functions.sort((left, right) => left.begin - right.begin);
  }
  const functionAt = (rva) => {
    let low = 0;
    let high = functions.length - 1;
    while (low <= high) {
      const middle = (low + high) >>> 1;
      const item = functions[middle];
      if (rva < item.begin) high = middle - 1;
      else if (rva >= item.end) low = middle + 1;
      else return item;
    }
    return null;
  };
  return {bytes, sections, functions, rvaToOffset, offsetToRva, functionAt};
}

function fingerprints(root) {
  const found = [];
  for (const name of fs.readdirSync(root)) {
    if (!name.endsWith(".h")) continue;
    const text = fs.readFileSync(path.join(root, name), "utf8");
    const expression = /Fingerprint\s+(\w+)\s*\{\s*(0x[0-9a-fA-F]+)\s*,\s*\{([\s\S]*?)\}\s*\}/g;
    for (const match of text.matchAll(expression)) {
      const values = [...match[3].matchAll(/0x([0-9a-fA-F]{1,2})/g)].map((part) => Number.parseInt(part[1], 16));
      if (!values.length) continue;
      found.push({file: name, name: match[1], rva: Number.parseInt(match[2], 16), bytes: Buffer.from(values)});
    }
  }
  return found;
}

function allOffsets(haystack, needle, limit = 3) {
  const result = [];
  for (let from = 0; result.length < limit;) {
    const at = haystack.indexOf(needle, from);
    if (at < 0) break;
    result.push(at);
    from = at + 1;
  }
  return result;
}

function fuzzyStarts(image, bytes) {
  const candidates = new Map();
  const chunkSize = 8;
  for (let chunkAt = 0; chunkAt + chunkSize <= bytes.length; chunkAt += chunkSize) {
    const chunk = bytes.subarray(chunkAt, chunkAt + chunkSize);
    for (const offset of allOffsets(image.bytes, chunk, 100)) {
      const rva = image.offsetToRva(offset);
      if (rva < chunkAt) continue;
      const start = rva - chunkAt;
      candidates.set(start, (candidates.get(start) || 0) + 1);
    }
  }
  const ranked = [...candidates.entries()].sort((left, right) => right[1] - left[1] || left[0] - right[0]);
  if (!ranked.length || ranked[0][1] < 2) return [];
  const best = ranked[0][1];
  return ranked.filter((entry) => entry[1] === best).map(([rva, chunks]) => ({rva: `0x${rva.toString(16)}`, chunks}));
}

function byteWindows(image, fn, width = 8) {
  const offset = image.rvaToOffset(fn.begin);
  const length = Math.min(fn.end - fn.begin, 4096);
  const result = new Set();
  if (offset < 0) return result;
  for (let at = 0; at + width <= length; at += width) {
    const value = image.bytes.subarray(offset + at, offset + at + width);
    let useful = false;
    for (const byte of value) {
      if (byte !== 0 && byte !== 0xcc) {
        useful = true;
        break;
      }
    }
    if (useful) result.add(value.toString("hex"));
  }
  return result;
}

function functionCandidates(oldImage, newImage, contract, predictedRva) {
  const oldFn = oldImage.functionAt(contract.rva);
  if (!oldFn || !Number.isFinite(predictedRva)) return [];
  const oldWindows = byteWindows(oldImage, oldFn);
  if (oldWindows.size < 3) return [];
  const radius = 0x40000;
  const ranked = [];
  for (const candidate of newImage.functions) {
    if (candidate.begin < predictedRva - radius || candidate.begin > predictedRva + radius) continue;
    const candidateWindows = byteWindows(newImage, candidate);
    let shared = 0;
    for (const value of candidateWindows) if (oldWindows.has(value)) ++shared;
    if (shared < 3) continue;
    const coverage = shared / oldWindows.size;
    ranked.push({
      rva: `0x${(candidate.begin + contract.rva - oldFn.begin).toString(16)}`,
      function_rva: `0x${candidate.begin.toString(16)}`,
      shared,
      old_windows: oldWindows.size,
      coverage: Number(coverage.toFixed(4)),
      distance: Math.abs(candidate.begin - predictedRva),
    });
  }
  ranked.sort((left, right) => right.coverage - left.coverage || right.shared - left.shared || left.distance - right.distance);
  return ranked.slice(0, 5);
}

const [headers, oldExe, newExe, output] = process.argv.slice(2);
if (!headers || !oldExe || !newExe || !output) {
  console.error("usage: node scripts/audit-game-build.cjs <build-headers> <old-skate.exe> <new-skate.exe> <report.json>");
  process.exit(2);
}

const oldImage = peImage(oldExe);
const newImage = peImage(newExe);
const contracts = fingerprints(headers);
const exactAnchors = [];
for (const contract of contracts) {
  const matches = allOffsets(newImage.bytes, contract.bytes).map((offset) => newImage.offsetToRva(offset)).filter((rva) => rva >= 0);
  if (matches.length === 1) exactAnchors.push({file: contract.file, oldRva: contract.rva, newRva: matches[0]});
}
function prediction(contract) {
  const ranked = exactAnchors
      .map((anchor) => ({...anchor, sameFile: anchor.file === contract.file, distance: Math.abs(anchor.oldRva - contract.rva)}))
      .sort((left, right) => Number(right.sameFile) - Number(left.sameFile) || left.distance - right.distance);
  const anchor = ranked[0];
  return anchor ? {rva: contract.rva + anchor.newRva - anchor.oldRva, anchor} : null;
}
const report = contracts.map((contract) => {
  const oldOffset = oldImage.rvaToOffset(contract.rva);
  const oldMatches = oldOffset >= 0 && oldImage.bytes.subarray(oldOffset, oldOffset + contract.bytes.length).equals(contract.bytes);
  const sameOffset = newImage.rvaToOffset(contract.rva);
  const sameRva = sameOffset >= 0 && newImage.bytes.subarray(sameOffset, sameOffset + contract.bytes.length).equals(contract.bytes);
  const matches = allOffsets(newImage.bytes, contract.bytes).map((offset) => newImage.offsetToRva(offset)).filter((rva) => rva >= 0);
  const fuzzy = matches.length ? [] : fuzzyStarts(newImage, contract.bytes);
  const predicted = prediction(contract);
  const functionMatches = matches.length === 1 || fuzzy.length === 1
      ? []
      : functionCandidates(oldImage, newImage, contract, predicted?.rva);
  return {
    file: contract.file,
    name: contract.name,
    old_rva: `0x${contract.rva.toString(16)}`,
    bytes: contract.bytes.length,
    old_contract_valid: oldMatches,
    same_rva: sameRva,
    new_matches: matches.map((rva) => `0x${rva.toString(16)}`),
    fuzzy_matches: fuzzy,
    predicted_rva: predicted ? `0x${predicted.rva.toString(16)}` : null,
    prediction_anchor: predicted ? `${predicted.anchor.file}:${predicted.anchor.oldRva.toString(16)}` : null,
    function_matches: functionMatches,
  };
});

const summary = {
  contracts: report.length,
  old_contracts_valid: report.filter((item) => item.old_contract_valid).length,
  unchanged_rva: report.filter((item) => item.same_rva).length,
  uniquely_relocated: report.filter((item) => !item.same_rva && item.new_matches.length === 1).length,
  uniquely_fuzzy: report.filter((item) => !item.same_rva && item.new_matches.length === 0 && item.fuzzy_matches.length === 1).length,
  uniquely_function_matched: report.filter((item) => item.new_matches.length !== 1 && item.fuzzy_matches.length !== 1 &&
      item.function_matches.length > 0 && item.function_matches[0].coverage >= 0.25 &&
      (item.function_matches.length === 1 || item.function_matches[0].coverage >= item.function_matches[1].coverage * 1.5)).length,
  unresolved: report.filter((item) => item.new_matches.length !== 1 && item.fuzzy_matches.length !== 1 &&
      !(item.function_matches.length > 0 && item.function_matches[0].coverage >= 0.25 &&
        (item.function_matches.length === 1 || item.function_matches[0].coverage >= item.function_matches[1].coverage * 1.5))).length,
};
fs.mkdirSync(path.dirname(output), {recursive: true});
const identity = (file, image) => ({
  file: path.basename(file),
  bytes: image.bytes.length,
  sha256: crypto.createHash("sha256").update(image.bytes).digest("hex"),
  runtime_functions: image.functions.length,
});
fs.writeFileSync(output, `${JSON.stringify({
  generated_at: new Date().toISOString(),
  old_image: identity(oldExe, oldImage),
  new_image: identity(newExe, newImage),
  summary,
  contracts: report,
}, null, 2)}\n`);
console.log(JSON.stringify(summary, null, 2));
