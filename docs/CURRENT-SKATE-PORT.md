# Current Steam Skate port

PUSH is independently porting to the current Steam build rather than tracking
ReSkate's older supported executable. PUSH does not distribute Skate binaries,
EA assets, Steam credentials, or depot contents. Players must own and install
Skate through Steam.

## Target identity

| Field | Current PUSH port target |
| --- | --- |
| Steam app | `3354750` |
| Steam depot | `3354751` |
| Steam build | `25617022` |
| Depot manifest | `4187479023593343011` |
| Data changelist | `29663206` |
| `Skate.exe` SHA-256 | `915522645e3b151a641743f52b6c7437e75b6d1381d8420edaa83493b025c4b2` |
| `steam_api64.dll` SHA-256 | `25d4c6a6e2e18cb1baada01e8946b03c08e41d7fc16337ecc1dc37bfe55f1e54` |

The machine-readable record is
[`Engine/Game/Build/ports/25617022.json`](../Engine/Game/Build/ports/25617022.json).

## Port status

The launcher and runtime accept the target above by exact file hash and PE
identity. PUSH. starts its DX12 overlay without installing any address-based
gameplay hooks from the previous executable. This is the safe runnable baseline
for completing the native port feature by feature.

This baseline is not yet a playable current-build release: it does not bypass
EA services, complete the EA main-menu transition, or enable native gameplay,
map, trainer, music, or cosmetic hooks. Those hooks are intentionally disabled
until their current-build contracts are reviewed. The launcher can prove the
current executable handoff and overlay startup, but it must not advertise that
as successful in-game support.

The first reproducible native-contract audit validates all 236 fingerprints
against the old supported image, then searches the new image without executing
either game binary.

| Result | Count | Meaning |
| --- | ---: | --- |
| Unique exact relocation | 104 | Identical full contract bytes at one new RVA |
| Function-body match | 37 | Unique bounded-function match using PE exception metadata |
| Partial fingerprint candidate | 50 | Unique partial match; manual review is still required |
| Unresolved | 45 | No sufficiently strong unique candidate yet |

The unresolved contracts are intentionally not copied into an active build
table. Most are profile/catalog, travel, music, Lua startup, media catalog, and
remote-collision boundaries. Activating incomplete addresses would turn a menu
action into an invalid native call, which is exactly the class of crash PUSH is
designed to prevent.

## Reproduce the audit

Use a legally installed copy of each build. Paths are examples and the report
is local research, so `analysis/` remains ignored by Git.

```powershell
node scripts\audit-game-build.cjs `
  Engine\Game\Build\20260929 `
  C:\path\to\old\Skate.exe `
  C:\path\to\current\Skate.exe `
  analysis\current-build-fingerprint-audit.json
```

The tool records hashes, PE runtime-function counts, exact matches, partial
matches, locality predictions, and bounded-function candidates. It never
launches or modifies either executable.

## Activation gate

The current public build remains pinned to its reviewed build table until all
of these are true:

1. Every candidate is manually reviewed and all unresolved contracts are found.
2. Non-fingerprinted globals, vtables, patch sites, and asset contracts are ported.
3. A complete sibling build table is reviewed rather than overwriting the old one.
4. The current world-layer count and a PUSH-owned content cache are recorded.
5. The full test suite passes and a local in-game smoke test confirms startup,
   menu navigation, maps, trainer actions, music, cosmetics, and shutdown.

This separation lets PUSH move to a newer game build without inheriting
ReSkate's release cadence or shipping EA-owned files.
