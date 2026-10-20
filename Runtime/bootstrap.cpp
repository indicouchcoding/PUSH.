#include "bootstrap.h"
#include "Engine/Core/Log/logging.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/launcher_support.h"
#include "Extension/Boot/startup_interventions.h"
#include "Extension/Console/commands.h"
#include "Extension/Boot/steam_restart_guard.h"
#include "Extension/Boot/user_data_redirect.h"
#include "Extension/Boot/ea_app_block.h"
#include "Extension/Boot/ea_service_block.h"
#include "Extension/Boot/descriptor_heap_size.h"
#include "Extension/Boot/game_fatal_dump.h"
#include "Extension/Boot/eadp_errors_arena.h"
#include "Extension/Assets/native_patch_support.h"
#include "Extension/Assets/mod_layers.h"
#include "Extension/Assets/loose_files.h"
#include "Extension/Scripting/custom_script_loader.h"
#include "Extension/Scripting/lua_native_block.h"
#include "Extension/Assets/morph_memory_pool.h"
#include "Extension/Assets/native_render_resource_pool.h"
#include "Extension/Rendering/display_startup.h"
#include "Extension/World/native_route_lookahead.h"
#include "Extension/World/native_entity_pages.h"
#include "Extension/World/world_pool_usage.h"
#include "Extension/World/physics_world_size.h"
#include "Extension/World/spatial_cull_ids.h"
#include <string>
#include <array>
#include <atomic>
#include <intrin.h>
#include <Windows.h>

namespace dingosdk::runtime {
namespace {
using CurrentOnlineEnabled = std::uint8_t (*)();
using CurrentClientTick = void (*)(std::uintptr_t, std::uintptr_t);
using CurrentDescriptionInit = void* (*)(void*);
using CurrentDescriptionDestroy = void (*)(void*);
using CurrentStringAssign = void (*)(void*, const char*, std::uint32_t);
using CurrentLocalRequest = void (*)(std::uintptr_t, const void*, const void*, std::uint32_t);
CurrentOnlineEnabled current_online_original{};
CurrentClientTick current_client_tick_original{};
std::uintptr_t current_image_base{};
std::atomic<std::uint64_t> current_online_invocations{};
std::atomic<std::uint64_t> current_online_interceptions{};
std::atomic_bool current_level_request_started{};
std::atomic<DWORD> current_client_state{MAXDWORD};
ULONGLONG current_level_request_after{};

constexpr std::uintptr_t current_client_tick_rva = 0x2e8dc80;
constexpr std::uintptr_t current_level_description_init_rva = 0x5c91c20;
constexpr std::uintptr_t current_level_description_destroy_rva = 0x516ce0;
constexpr std::uintptr_t current_string_assign_rva = 0x158b200;
constexpr std::uintptr_t current_local_level_request_rva = 0x2e896a0;

template<std::size_t N>
bool current_fingerprint_matches(std::uintptr_t base, std::uintptr_t rva,
                                 const std::array<unsigned char, N>& fingerprint) {
    std::array<unsigned char, N> actual{};
    SIZE_T read{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + rva),
               actual.data(), actual.size(), &read) &&
           read == actual.size() && actual == fingerprint;
}

std::uint8_t current_online_selective() {
    constexpr std::array<std::uintptr_t, 4> offline_return_rvas{
        0x284384, // Backend availability predicate.
        0x82f316, // Async character preparation.
        0x831fd5, // Character readiness.
        0x848ba9  // Post-spawn progression.
    };

    const auto incoming_error = GetLastError();
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    bool replace_result{};
    for (const auto rva : offline_return_rvas) {
        if (return_address == current_image_base + rva) {
            replace_result = true;
            break;
        }
    }
    ++current_online_invocations;

    SetLastError(incoming_error);
    auto native_result = current_online_original();
    const auto native_error = GetLastError();
    if (replace_result) {
        ++current_online_interceptions;
        native_result = 0;
    }
    SetLastError(native_error);
    return native_result;
}

void current_client_tick(std::uintptr_t client, std::uintptr_t update) {
    current_client_tick_original(client, update);
    const auto native_error = GetLastError();

    DWORD state{};
    SIZE_T state_read{};
    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(client + 0xc4),
            &state, sizeof(state), &state_read) && state_read == sizeof(state)) {
        const auto previous = current_client_state.exchange(state);
        if (previous != state)
            logging::log(logging::Level::info, logging::Channel::level,
                "Current-build client state: {} -> {}.", previous, state);
    }

    bool expected = false;
    if (client && GetTickCount64() >= current_level_request_after &&
        current_level_request_started.compare_exchange_strong(expected, true)) {
        struct Description {
            alignas(8) std::array<unsigned char, 0x30> bytes{};
            CurrentDescriptionDestroy destroy{};
            explicit Description(std::uintptr_t base)
                : destroy(reinterpret_cast<CurrentDescriptionDestroy>(
                      base + current_level_description_destroy_rva)) {
                reinterpret_cast<CurrentDescriptionInit>(
                    base + current_level_description_init_rva)(bytes.data());
            }
            ~Description() { destroy(bytes.data()); }
        } description(current_image_base);

        constexpr char root[] = "Levels/Game/DingoLevel_Root/DingoLevel_Root";
        constexpr char san_van[] = "levels/game/BAM_LevelRoot/BAM_LevelRoot";
        const auto assign = reinterpret_cast<CurrentStringAssign>(
            current_image_base + current_string_assign_rva);
        assign(description.bytes.data() + 0x10, root, static_cast<std::uint32_t>(sizeof(root) - 1));
        assign(description.bytes.data() + 0x20, san_van,
            static_cast<std::uint32_t>(sizeof(san_van) - 1));
        description.bytes[0x2c] = 0;

        logging::write(logging::Level::info, logging::Channel::level,
            "Current-build local startup: requesting San Vansterdam from the game update thread.");
        reinterpret_cast<CurrentLocalRequest>(current_image_base + current_local_level_request_rva)(
            client, description.bytes.data(), nullptr, 0);
    }

    SetLastError(native_error);
}

bool start_current_level_transition(std::uintptr_t base) {
    constexpr std::array<unsigned char, 17> tick_fingerprint{
        0x40,0x53,0x57,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x38,0x8b,0x81,0xc4,0,0,0};
    constexpr std::array<unsigned char, 15> init_fingerprint{
        0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xe2,0x1f,0x15,0xfe,0x48};
    constexpr std::array<unsigned char, 15> destroy_fingerprint{
        0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0xf1,0x48,0x83};
    constexpr std::array<unsigned char, 15> assign_fingerprint{
        0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48};
    constexpr std::array<unsigned char, 15> request_fingerprint{
        0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x28,0x48,0x8b};
    const bool contracts_ready =
        current_fingerprint_matches(base, current_client_tick_rva, tick_fingerprint) &&
        current_fingerprint_matches(base, current_level_description_init_rva, init_fingerprint) &&
        current_fingerprint_matches(base, current_level_description_destroy_rva, destroy_fingerprint) &&
        current_fingerprint_matches(base, current_string_assign_rva, assign_fingerprint) &&
        current_fingerprint_matches(base, current_local_level_request_rva, request_fingerprint);
    if (!contracts_ready) {
        logging::write(logging::Level::error, logging::Channel::level,
            "Current-build local startup skipped: one or more native contracts did not match.");
        return false;
    }

    void* trampoline{};
    auto* target = reinterpret_cast<void*>(base + current_client_tick_rva);
    const auto prepared = hook_prepare(target, reinterpret_cast<void*>(&current_client_tick), &trampoline);
    if (prepared != HookOk || !trampoline) return false;
    current_client_tick_original = reinterpret_cast<CurrentClientTick>(trampoline);
    current_level_request_started = false;
    current_client_state = MAXDWORD;
    current_level_request_after = GetTickCount64() + 25000;
    const auto enabled = hook_enable(target);
    if (enabled != HookOk) {
        (void)hook_remove(target);
        current_client_tick_original = nullptr;
        return false;
    }
    logging::write(logging::Level::info, logging::Channel::level,
        "Current-build local startup armed; San Vansterdam will be requested after client startup settles.");
    return true;
}

bool start_current_offline_gate(std::uintptr_t base) {
    // Build 25617022 moved the same ReSkate OnlineEnabled leaf from the
    // previous build's 0x3b8830 to 0x3bb110. Keep this entirely fingerprint
    // guarded: a Steam update must fall back to the compatibility path rather
    // than detouring an unrelated function.
    // The signature is at raw file offset 0x3bb110. The current image maps
    // that section 0xa00 bytes higher in virtual memory.
    constexpr std::uintptr_t online_enabled_rva = 0x3bbb10;
    constexpr std::array<unsigned char, 16> fingerprint{
        0xb0,0x01,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,
        0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
    std::array<unsigned char, fingerprint.size()> actual{};
    SIZE_T read{};
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + online_enabled_rva),
            actual.data(), actual.size(), &read) || read != actual.size() || actual != fingerprint) {
        logging::write(logging::Level::warning, logging::Channel::runtime,
            "Current-build offline gate skipped: OnlineEnabled fingerprint did not match.");
        return false;
    }
    void* trampoline{};
    auto* target = reinterpret_cast<void*>(base + online_enabled_rva);
    current_image_base = base;
    const auto prepared = hook_prepare(target, reinterpret_cast<void*>(&current_online_selective), &trampoline);
    if (prepared != HookOk || !trampoline) {
        logging::log(logging::Level::warning, logging::Channel::runtime,
            "Current-build offline gate could not prepare (status {}).", static_cast<long>(prepared));
        return false;
    }
    current_online_original = reinterpret_cast<CurrentOnlineEnabled>(trampoline);
    const auto enabled = hook_enable(target);
    if (enabled != HookOk) {
        (void)hook_remove(target);
        current_online_original = nullptr;
        logging::log(logging::Level::warning, logging::Channel::runtime,
            "Current-build offline gate could not enable (status {}).", static_cast<long>(enabled));
        return false;
    }
    logging::log(logging::Level::info, logging::Channel::runtime,
        "Current-build selective offline gate enabled at {:#x}; four relocated startup callers are intercepted.",
        online_enabled_rva);
    return true;
}

bool stage(logging::Channel channel, const char* name, bool ready, const std::string& error) {
    logging::log(ready ? logging::Level::info : logging::Level::error, channel,
        "{}: {}{}", name, ready ? "ready" : "failed", error.empty() ? "" : " - " + error);
    return ready;
}
}

bool initialize_compatibility_bootstrap(std::uintptr_t base) {
    using logging::Channel;
    const auto hooks = hook_initialize();
    if (hooks != HookOk && hooks != HookAlreadyInitialized) {
        logging::log(logging::Level::error, Channel::hooks,
            "Compatibility hook service failed: {} ({})", hook_status_string(hooks), static_cast<long>(hooks));
        return false;
    }
    logging::write(logging::Level::info, Channel::hooks,
        "Compatibility hook service ready (platform guards only).");

    std::string error;
    error.clear();
    if (!stage(Channel::runtime, "EA app launch block", start_ea_app_block(error), error)) return false;
    error.clear();
    if (!stage(Channel::runtime, "EA online services block", start_ea_service_block(error), error)) return false;
    if (!start_current_offline_gate(base)) return false;
    if (!start_current_level_transition(base)) return false;
    error.clear();
    // This hook is independent of Skate's image and prevents the game from
    // handing control back to Steam's restart path after PUSH starts it.
    if (!stage(Channel::runtime, "Steam restart guard", start_steam_restart_guard(error), error)) return false;
    return true;
}

bool initialize_bootstrap(std::uintptr_t base) {
    if (!base) return false;
    using logging::Channel;
    std::string error;
    bool ready = start_startup_interventions(base, error);
    if (!stage(Channel::runtime, "Startup interventions", ready, error)) return false;
    const auto hooks = hook_initialize();
    if (hooks != HookOk && hooks != HookAlreadyInitialized) {
        logging::log(logging::Level::error, Channel::hooks, "Hook service failed: {} ({})", hook_status_string(hooks), static_cast<long>(hooks));
        return false;
    }
    logging::write(logging::Level::info, Channel::hooks, "Microsoft Detours hook service ready.");
    // The memory ledger, when RESKATE_MEMORY_TRACE asks for it: here, while the game is still
    // suspended and before the pools below are enlarged, so it sees who makes all of them.
    console::start_memory_trace_from_environment();
    // (Not needed for the game to run: a mismatch leaves crash reports at what the game writes.)
    error.clear(); ready = start_game_fatal_dump(base, error);
    (void)stage(Channel::runtime, "Crash dumps from the game's own fatal handlers", ready, error);
    error.clear(); ready = start_native_patch_support(base, error);
    if (!stage(Channel::assets, "Native InitFS/package support", ready, error)) return false;
    error.clear(); ready = start_mod_layers(base, error);
    if (!stage(Channel::assets, "Mod layout layers", ready, error)) return false;
    // Before anything that lets a script of the player's own into the game's Lua.
    error.clear(); ready = start_lua_native_block(base, error);
    if (!stage(Channel::assets, "Lua kept to Lua (no DLLs, programs, file writes or bytecode)", ready, error)) return false;
    error.clear(); ready = start_loose_files(base, error);
    if (!stage(Channel::assets, "Loose Lua/config files", ready, error)) return false;
    error.clear(); ready = start_custom_script_loader(base, error);
    if (!stage(Channel::assets, "Custom Lua scripts", ready, error)) return false;
    error.clear(); ready = start_morph_memory_pool(base, error);
    if (!stage(Channel::assets, "Morph memory pools", ready, error)) return false;
    error.clear(); ready = start_native_render_resource_pool(base, error);
    if (!stage(Channel::assets, "Native render-resource pools", ready, error)) return false;
    error.clear(); ready = start_native_entity_pages(base, error);
    if (!stage(Channel::world, "Native entity pages (524288 entity/update slots, 1048576 render slots, 16384 spatial blocks/bucket, 128 MiB arena minimum)", ready, error)) return false;
    error.clear(); ready = start_spatial_cull_ids(base, error);
    if (!stage(Channel::world, "Spatial cull IDs (262144 a queue)", ready, error)) return false;
    // (Diagnostics only: how full those pools get. A mismatch leaves the game as it is.)
    error.clear(); ready = start_world_pool_usage(base, error);
    (void)stage(Channel::world, "World pool usage watch", ready, error);
    error.clear(); ready = start_physics_world_size(base, error);
    if (!stage(Channel::world, "Physics world pools (65000 static bodies)", ready, error)) return false;
    error.clear(); ready = start_native_route_lookahead(base, error);
    if (!stage(Channel::world, "NPC route cycle and endpoint guards", ready, error)) return false;
    // The level unload guard (unload_guard.h) is not installed: stepping over an entry with no
    // asset stopped the crash, but that entry had been registered when the level loaded, and
    // left registered it kept the level from ever finishing its unload (a map change that never
    // ends, which is worse than the crash). It goes back in once it can release what it skips.
    if (!stage(Channel::graphics, "Display startup settings", start_display_settings(base), {})) return false;
    // (Not needed for the game to run: a mismatch leaves the heaps at the game's size, and only
    // the densest levels outgrow them.)
    error.clear(); ready = start_descriptor_heap_size(base, error);
    (void)stage(Channel::graphics, "Descriptor heaps: 1,000,000, with 524,288 a frame", ready, error);
    error.clear(); ready = start_user_data_redirect(error);
    if (!stage(Channel::runtime, "Separate game user data", ready, error)) return false;
    error.clear(); ready = start_ea_app_block(error);
    if (!stage(Channel::runtime, "EA app launch block", ready, error)) return false;
    error.clear(); ready = start_ea_service_block(error);
    if (!stage(Channel::runtime, "EA online services block", ready, error)) return false;
    // (Not needed for the game to run: a mismatch leaves the arena at the game's size.)
    error.clear(); ready = start_eadp_errors_arena(base, error);
    (void)stage(Channel::runtime, "EA error-report arena capped at 32 MiB", ready, error);
    error.clear(); ready = start_steam_restart_guard(error);
    return stage(Channel::runtime, launcher::offline_mode() ? "Offline Steam" : "Steam restart guard", ready, error);
}
}
