#include "gui_internal.h"

#include "text_encoding.h"

#include "Engine/Core/Json/json.h"
#include "Engine/Core/Log/logging.h"

#include <algorithm>
#include <format>

// PUSH. legal notes are bundled with the launcher. The fork deliberately does
// not query or present policy documents owned by the upstream project.
namespace dingosdk::launcher_gui::detail {
namespace {
constexpr const char* names[Legal::count]{"PRIVACY POLICY", "TERMS OF SERVICE"};

void start_fetch(Legal& legal, int kind) {
    auto& slot = legal.slots[static_cast<std::size_t>(kind)];
    slot.error.clear();
    slot.document.title = kind == Legal::privacy ? "PUSH. Privacy" : "PUSH. Terms";
    slot.document.updated = "2026-10-19";
    slot.document.version = 1;
    slot.document.markdown = kind == Legal::privacy
        ? "PUSH. does not send crash reports or performance telemetry by default. Steam authentication is used only to install and launch a game you own. Local profiles, settings, logs, mods and caches remain on your PC. Thunderstore and GitHub requests follow those services' own policies."
        : "PUSH. is a free, community-maintained fan project distributed under the GNU GPL v3. It is not affiliated with or endorsed by Electronic Arts, Full Circle, or the PUSH. maintainers. Use your own copy of skate., respect other players, and install third-party mods only when you trust their source.";
    slot.text = thunderstore::readme_lines(slot.document.markdown, 2000);
    slot.loaded = true;
    slot.loading = false;
}
} // namespace

void legal_window(const Fonts& fonts, ImVec2 size, Ui& ui) {
    auto& legal = ui.legal;
    const int kind = legal.open;
    auto& slot = legal.slots[static_cast<std::size_t>(kind)];
    {
        std::lock_guard lock(legal.mutex);
        for (auto& each : legal.slots)
            if (each.arrived) {
                each.arrived = false;
                each.error = std::move(each.incoming_error);
                if (each.error.empty()) {
                    each.document = std::move(each.incoming);
                    each.text = thunderstore::readme_lines(each.document.markdown, 2000);
                    each.loaded = true;
                }
                each.incoming = {};
            }
    }
    if (!slot.loaded && !slot.loading && slot.error.empty()) start_fetch(legal, kind);

    const auto panel = begin_panel("##legal_panel", size, ImVec2(S(880), S(680)));
    panel_title(fonts, names[kind]);
    const float footer = ImGui::GetFrameHeight() + S(24) + S(12);
    ImGui::BeginChild("##legal_body", ImVec2(0, panel.y - ImGui::GetCursorPosY() - footer), ImGuiChildFlags_NavFlattened);
    if (slot.loading && !slot.loaded) {
        ImGui::TextDisabled("Loading...");
    } else if (!slot.loaded) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Could not load it: %s", slot.error.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Try again", ImVec2(S(120), 0))) start_fetch(legal, kind);
    } else {
        const auto& document = slot.document;
        ImGui::PushFont(fonts.heading);
        ImGui::TextUnformatted(document.title.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("Version %llu, updated %s", static_cast<unsigned long long>(document.version), document.updated.c_str());
        ImGui::Dummy(ImVec2(0, S(6)));
        draw_readme(fonts, slot.text);
    }
    ImGui::EndChild();

    ImGui::SetCursorPosY(panel.y - S(24) - ImGui::GetFrameHeight());
    const int other = kind == Legal::privacy ? Legal::terms : Legal::privacy;
    if (ImGui::Button(kind == Legal::privacy ? "Terms of Service" : "Privacy Policy", ImVec2(S(180), 0))) legal.open = other;
    ImGui::SameLine();
    ImGui::SetCursorPosX(panel.x - S(28) - S(110));
    const bool close = ImGui::Button("CLOSE", ImVec2(S(110), 0));
    default_focus();
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) legal.open = -1;
    ImGui::End();
}

void check_legal_versions(Ui& ui, const Settings& settings) {
    (void)settings;
    ui.legal.versions_started = true;
    ui.legal.agreement = false;
}

void legal_agreement_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, Ui& ui, HWND window) {
    auto& legal = ui.legal;
    auto& settings = launcher.settings();
    const bool first = !settings.accepted_privacy && !settings.accepted_terms;
    const auto panel = begin_panel("##legal_agreement", size, ImVec2(S(640), S(470)));
    panel_title(fonts, first ? "BEFORE YOU PLAY" : "UPDATED TERMS");
    ImGui::PushTextWrapPos(0);
    if (first) {
        ImGui::TextUnformatted("PUSH. has terms of service and a privacy policy. Read them, and agree to keep using PUSH.");
    } else {
        std::string changed;
        if (legal.current[Legal::terms] > settings.accepted_terms) changed = "terms of service";
        if (legal.current[Legal::privacy] > settings.accepted_privacy)
            changed += changed.empty() ? "privacy policy" : " and the privacy policy";
        ImGui::TextUnformatted(std::format("The {} changed since you last agreed. Read what changed, and agree to keep using "
                                           "PUSH.", changed).c_str());
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::TextDisabled("In short:");
    // (BulletText does not wrap: a bullet, then wrapped text beside it.)
    for (const char* line : {"Play fair online and respect other players.",
                             "Don't unlock or copy cosmetics that skate sells.",
                             "Crash reports go to the PUSH. team unless you turn them off in Settings. Nothing in them is "
                             "meant to identify you.",
                             "PUSH. is a free fan project, not made by EA, and comes with no warranty."}) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", line);
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(10)));
    if (ImGui::Button("Read the Terms of Service", ImVec2(S(260), 0))) legal.open = Legal::terms;
    ImGui::SameLine();
    if (ImGui::Button("Read the Privacy Policy", ImVec2(S(260), 0))) legal.open = Legal::privacy;

    ImGui::SetCursorPosY(panel.y - S(24) - ImGui::GetFrameHeight());
    if (ImGui::Button("QUIT", ImVec2(S(110), 0))) PostMessageW(window, WM_CLOSE, 0, 0);
    ImGui::SameLine();
    ImGui::SetCursorPosX(panel.x - S(28) - S(160));
    push_primary_button();
    const bool agree = ImGui::Button("I AGREE", ImVec2(S(160), 0));
    pop_primary_button();
    default_focus();
    if (agree) {
        settings.accepted_privacy = (std::max)(settings.accepted_privacy, legal.current[Legal::privacy]);
        settings.accepted_terms = (std::max)(settings.accepted_terms, legal.current[Legal::terms]);
        launcher.save();
        legal.agreement = false;
        logging::write(logging::Level::info, logging::Channel::launcher,
                       std::format("Agreed to privacy policy {} and terms {}.", settings.accepted_privacy, settings.accepted_terms));
    }
    ImGui::End();
}

} // namespace dingosdk::launcher_gui::detail
