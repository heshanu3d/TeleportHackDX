#include "services/feature_service.h"

namespace th {

void FeatureService::set_jump_gravity(double value) {
    if (version_.jump_gravity) {
        backend_.write_float(version_.jump_gravity, static_cast<float>(value));
    }
}

bool FeatureService::toggle_anti_jump(bool& now_on) {
    if (!version_.anti_jump) {
        last_error_ = "anti_jump not supported on this client";
        return false;
    }
    uint8_t val;
    if (!backend_.read_byte(version_.anti_jump, val)) {
        last_error_ = "failed to read anti_jump byte";
        return false;
    }
    if (val == 0x75) {
        backend_.write_byte(version_.anti_jump, 0xEB);
        set_jump_gravity(0.0);
        now_on = true;
        return true;
    }
    if (val == 0xEB) {
        backend_.write_byte(version_.anti_jump, 0x75);
        set_jump_gravity(-7.0);
        now_on = false;
        return true;
    }
    last_error_ = "unknown anti_jump byte state";
    return false;
}

bool FeatureService::toggle_autoloot(bool& now_on) {
    if (!version_.autoloot_2) {
        last_error_ = "autoloot not supported on this client";
        return false;
    }
    uint8_t b0, b1;
    if (!backend_.read_byte(version_.autoloot_2, b0) ||
        !backend_.read_byte(version_.autoloot_2 + 1, b1)) {
        last_error_ = "failed to read autoloot bytes";
        return false;
    }
    if (b0 == 0x74 && b1 == 0x10) {
        backend_.write_byte(version_.autoloot_2, 0x90);
        backend_.write_byte(version_.autoloot_2 + 1, 0x90);
        now_on = true;
        return true;
    }
    if (b0 == 0x90 && b1 == 0x90) {
        backend_.write_byte(version_.autoloot_2, 0x74);
        backend_.write_byte(version_.autoloot_2 + 1, 0x10);
        now_on = false;
        return true;
    }
    last_error_ = "unknown autoloot byte state";
    return false;
}

bool FeatureService::toggle_patch_loot(bool& now_on) {
    if (!version_.patch_loot || !version_.patch_loot2 || !version_.patch_lootslot) {
        last_error_ = "patch_loot not supported on this client";
        return false;
    }
    uint8_t v0, v1, v2;
    if (!backend_.read_byte(version_.patch_loot, v0) ||
        !backend_.read_byte(version_.patch_loot2, v1) ||
        !backend_.read_byte(version_.patch_lootslot, v2)) {
        last_error_ = "failed to read patch_loot bytes";
        return false;
    }
    if (v0 == 0x72 && v1 == 0x72 && v2 == 0x01) {
        backend_.write_byte(version_.patch_loot, 0xEB);
        backend_.write_byte(version_.patch_loot2, 0xEB);
        backend_.write_byte(version_.patch_lootslot, 0x00);
        now_on = true;
        return true;
    }
    if (v0 == 0xEB && v1 == 0xEB && v2 == 0x00) {
        backend_.write_byte(version_.patch_loot, 0x72);
        backend_.write_byte(version_.patch_loot2, 0x72);
        backend_.write_byte(version_.patch_lootslot, 0x01);
        now_on = false;
        return true;
    }
    last_error_ = "unknown patch_loot byte state";
    return false;
}

namespace {
constexpr uint8_t kLuaOff[6] = {0x56, 0x8B, 0xF1, 0x0F, 0x84, 0xB1};
constexpr uint8_t kLuaOn[6] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
} // namespace

bool FeatureService::toggle_lua_unlock(bool& now_on) {
    if (!version_.lua_unlock) {
        last_error_ = "lua_unlock not supported on this client";
        return false;
    }
    uint8_t current[6];
    for (int i = 0; i < 6; ++i) {
        if (!backend_.read_byte(version_.lua_unlock + i, current[i])) {
            last_error_ = "failed to read lua_unlock bytes";
            return false;
        }
    }
    bool is_off = true, is_on = true;
    for (int i = 0; i < 6; ++i) {
        if (current[i] != kLuaOff[i]) is_off = false;
        if (current[i] != kLuaOn[i]) is_on = false;
    }
    if (is_off) {
        for (int i = 0; i < 6; ++i) backend_.write_byte(version_.lua_unlock + i, kLuaOn[i]);
        now_on = true;
        return true;
    }
    if (is_on) {
        for (int i = 0; i < 6; ++i) backend_.write_byte(version_.lua_unlock + i, kLuaOff[i]);
        now_on = false;
        return true;
    }
    last_error_ = "unknown lua_unlock byte state";
    return false;
}

bool FeatureService::set_speed(double speed) {
    if (!version_.supports_speed()) {
        last_error_ = "speed not supported on this client";
        return false;
    }
    uintptr_t pb1, pb2, base;
    if (!backend_.read_pointer(version_.static_player, pb1) ||
        !backend_.read_pointer(pb1 + version_.pb_pointer1, pb2) ||
        !backend_.read_pointer(pb2 + version_.pb_pointer2, base)) {
        last_error_ = "failed to resolve player base for speed";
        return false;
    }
    if (!backend_.write_float(base + version_.speed_global, static_cast<float>(speed))) {
        last_error_ = "failed to write speed";
        return false;
    }
    return true;
}

} // namespace th
