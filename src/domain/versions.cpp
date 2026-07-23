#include "domain/versions.h"

namespace th {

namespace {

const std::vector<uintptr_t> kVanillaOffsetsX = {0x88, 0x28, 0x708, 0xC, 0x2A8};
const std::vector<uintptr_t> kVanillaOffsetsY = {0x88, 0x28, 0x6B4, 0x3C, 0x2C8};
const std::vector<uintptr_t> kVanillaOffsetsZ = {0x88, 0x28, 0x7C8, 0x1A4, 0x54};

GameVersion make_1_12_1() {
    GameVersion v;
    v.name = "1.12.1";
    v.executable = "WoW.exe";
    v.static_player = 0x00C7BCD4;
    v.curr_pos_x = 0x00C7B548;
    v.curr_pos_y = 0x00C7B544;
    v.curr_pos_z = 0x00C7B54C;
    v.anti_jump = 0x7C625F;
    v.jump_gravity = 0x7C6272;
    v.player_name = 0xC27D88;
    v.autoloot_2 = 0x4C1ECF;
    v.patch_loot = 0x4C21C0;
    v.patch_loot2 = 0x4C28FF;
    v.patch_lootslot = 0x4C2E94;
    v.lua_unlock = 0x494A57;
    v.dst_x_offsets = kVanillaOffsetsX;
    v.dst_y_offsets = kVanillaOffsetsY;
    v.dst_z_offsets = kVanillaOffsetsZ;
    return v;
}

GameVersion make_1_12_3() {
    GameVersion v;
    v.name = "1.12.3";
    v.executable = "WoW.exe";
    v.static_player = 0x00C803F4;
    v.curr_pos_x = 0x00C7FC64;
    v.curr_pos_y = 0x00C7FC68;
    v.curr_pos_z = 0x00C7FC6C;
    v.anti_jump = 0x7C973F;
    v.jump_gravity = 0x7C9752;
    v.player_name = 0xC2C430;
    v.autoloot_2 = 0x4C2CBF;
    v.patch_loot = 0x4C2FB0;
    v.patch_loot2 = 0x4C36EF;
    v.patch_lootslot = 0x4C3C96;
    v.lua_unlock = 0x495847;
    v.dst_x_offsets = kVanillaOffsetsX;
    v.dst_y_offsets = kVanillaOffsetsY;
    v.dst_z_offsets = kVanillaOffsetsZ;
    return v;
}

GameVersion make_3_3_5() {
    GameVersion v;
    v.name = "3.3.5";
    v.executable = "WoW.exe";
    v.static_player = 0x00CD87A8;
    v.player_name = 0xC79D10 + 8;
    v.map_id = 0xAB63BC;
    v.pb_pointer1 = 0x34;
    v.pb_pointer2 = 0x24;
    v.pos_x = 0x798;
    v.pos_y = 0x79C;
    v.pos_z = 0x7A0;
    v.pos_r = 0x7A8;
    v.speed_global = 0x814;
    return v;
}

} // namespace

const std::vector<GameVersion>& all_versions() {
    static const std::vector<GameVersion> versions = {
        make_1_12_1(),
        make_1_12_3(),
        make_3_3_5(),
    };
    return versions;
}

const GameVersion* find_version(const std::string& name) {
    for (const auto& v : all_versions()) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

} // namespace th
