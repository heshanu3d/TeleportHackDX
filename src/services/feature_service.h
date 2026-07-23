// Toggle services for memory-patch features (anti-jump, autoloot,
// lua-unlock, patch-loot, speed).
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/feature_toggles.py
#pragma once

#include <string>

#include "domain/versions.h"
#include "memory/memory_backend.h"

namespace th {

class FeatureService {
public:
    FeatureService(MemoryBackend& backend, const GameVersion& version)
        : backend_(backend), version_(version) {}

    // Each toggle returns true+`now_on` on success, false on failure
    // (unsupported client or unrecognised current byte pattern).
    bool toggle_anti_jump(bool& now_on);
    bool toggle_autoloot(bool& now_on);
    bool toggle_patch_loot(bool& now_on);
    bool toggle_lua_unlock(bool& now_on);

    bool set_speed(double speed);

    const std::string& last_error() const { return last_error_; }

private:
    MemoryBackend& backend_;
    const GameVersion& version_;
    std::string last_error_;

    void set_jump_gravity(double value);
};

} // namespace th
