// High-level teleport service.
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/teleport_service.py
//
// Centralises:
//  * address resolution (pointer chain for 3.3.5, multi-level offsets for
//    1.12.x)
//  * coordinate-axis swapping (1.12.x stores Y/X swapped relative to the UI)
//  * step-by-step teleporting (smooth movement)
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "domain/models.h"
#include "domain/versions.h"
#include "memory/memory_backend.h"

namespace th {

struct StepConfig {
    double distance_per_step = 10.0;
    double sleep_between_steps_sec = 0.04;
};

class TeleportService {
public:
    TeleportService(MemoryBackend& backend, const GameVersion& version)
        : backend_(backend), version_(version) {}

    // Returns false and sets last_error() on failure.
    bool read_position(Position& out);
    bool write_position(const Position& pos);

    // Walks to `target` in equal increments. `sleeper` defaults to
    // Sleep(); overridable for tests. Returns the number of steps taken
    // via `steps_out` (0 on failure).
    bool teleport_step(const Position& target, const StepConfig& cfg, int& steps_out,
                        const std::function<void(double)>& sleeper = nullptr);

    bool read_map_id(uint32_t& out);

    const std::string& last_error() const { return last_error_; }

private:
    MemoryBackend& backend_;
    const GameVersion& version_;
    std::string last_error_;

    bool resolve_player_base(uintptr_t& out);
    bool resolve_vanilla_write_addresses(uintptr_t& ax, uintptr_t& ay, uintptr_t& az);
};

// Non-blocking equivalent of TeleportService::teleport_step, meant to be
// advanced once per rendered frame from the D3D9 hook loop. A blocking
// Sleep()-based loop (like the Python/AutoIt originals use) would freeze
// the whole game for the duration of the walk since we now run on the
// game's own thread, so every "step" tick instead happens opportunistically
// once its time budget has elapsed.
class TeleportStepSession {
public:
    void start(TeleportService& svc, const Position& target, const StepConfig& cfg);
    // Call every frame (any thread that owns `svc`'s backend). Returns true
    // while the session is still advancing.
    bool tick(TeleportService& svc);
    bool active() const { return active_; }

private:
    bool active_ = false;
    Position delta_{};
    Position cursor_{};
    int steps_remaining_ = 0;
    double interval_sec_ = 0.04;
    double accum_sec_ = 0.0;
    long long last_tick_qpc_ = 0;
};

} // namespace th
