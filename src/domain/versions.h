// Per-WoW-client memory layout descriptors.
//
// Ported verbatim from
// TeleportHackOnVanilla/win/src/teleport_hack/domain/versions.py
// (which itself was lifted from the main*.au3 / global.au3 entry points).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace th {

struct GameVersion {
    std::string name;
    std::string executable = "WoW.exe";
    std::string window_class = "GxWindowClassD3d";

    // Static addresses
    uintptr_t static_player = 0;
    uintptr_t curr_pos_x = 0;
    uintptr_t curr_pos_y = 0;
    uintptr_t curr_pos_z = 0;
    uintptr_t anti_jump = 0;
    uintptr_t jump_gravity = 0;
    uintptr_t player_name = 0;
    uintptr_t map_id = 0;

    // Pointer offsets (3.3.5 only)
    uintptr_t pb_pointer1 = 0;
    uintptr_t pb_pointer2 = 0;
    uintptr_t pos_x = 0;
    uintptr_t pos_y = 0;
    uintptr_t pos_z = 0;
    uintptr_t pos_r = 0;
    uintptr_t speed_global = 0;

    // Multi-level write offsets (vanilla / 1.12.x only)
    std::vector<uintptr_t> dst_x_offsets;
    std::vector<uintptr_t> dst_y_offsets;
    std::vector<uintptr_t> dst_z_offsets;

    // Code-patch addresses
    uintptr_t autoloot_2 = 0;
    uintptr_t patch_loot = 0;
    uintptr_t patch_loot2 = 0;
    uintptr_t patch_lootslot = 0;
    uintptr_t lua_unlock = 0;

    bool supports_speed() const { return speed_global != 0; }
    bool supports_anti_jump() const { return anti_jump != 0; }
    bool supports_autoloot() const { return autoloot_2 != 0; }
    bool is_pointer_chain() const { return pb_pointer1 != 0 || pb_pointer2 != 0; }
    bool is_vanilla() const { return name.rfind("1.12", 0) == 0; }
};

// Index into a fixed list so the UI can offer a simple dropdown.
const std::vector<GameVersion>& all_versions();
const GameVersion* find_version(const std::string& name);

} // namespace th
