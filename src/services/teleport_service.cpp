#include "services/teleport_service.h"

#include <Windows.h>
#include <cmath>

namespace th {

bool TeleportService::resolve_player_base(uintptr_t& out) {
    uintptr_t pb1, pb2;
    if (!backend_.read_pointer(version_.static_player, pb1)) {
        last_error_ = "failed to read static_player pointer";
        return false;
    }
    if (!backend_.read_pointer(pb1 + version_.pb_pointer1, pb2)) {
        last_error_ = "failed to read pb_pointer1 chain";
        return false;
    }
    if (!backend_.read_pointer(pb2 + version_.pb_pointer2, out)) {
        last_error_ = "failed to read pb_pointer2 chain";
        return false;
    }
    return true;
}

bool TeleportService::resolve_vanilla_write_addresses(uintptr_t& ax, uintptr_t& ay,
                                                       uintptr_t& az) {
    ax = ay = az = version_.static_player;
    size_t n = version_.dst_x_offsets.size();
    for (size_t i = 0; i < n; ++i) {
        uintptr_t px, py, pz;
        if (!backend_.read_pointer(ax, px) || !backend_.read_pointer(ay, py) ||
            !backend_.read_pointer(az, pz)) {
            last_error_ = "failed to walk vanilla offset chain";
            return false;
        }
        ax = px + version_.dst_x_offsets[i];
        ay = py + version_.dst_y_offsets[i];
        az = pz + version_.dst_z_offsets[i];
    }
    return true;
}

bool TeleportService::read_position(Position& out) {
    if (version_.is_pointer_chain()) {
        uintptr_t base;
        if (!resolve_player_base(base)) return false;
        float x, y, z;
        if (!backend_.read_float(base + version_.pos_x, x) ||
            !backend_.read_float(base + version_.pos_y, y) ||
            !backend_.read_float(base + version_.pos_z, z)) {
            last_error_ = "failed to read position floats";
            return false;
        }
        std::optional<double> orientation;
        if (version_.pos_r) {
            float r;
            if (!backend_.read_float(base + version_.pos_r, r)) {
                last_error_ = "failed to read orientation float";
                return false;
            }
            orientation = static_cast<double>(r);
        }
        out = Position{x, y, z, orientation};
        return true;
    }

    float x, y, z;
    if (!backend_.read_float(version_.curr_pos_x, x) ||
        !backend_.read_float(version_.curr_pos_y, y) ||
        !backend_.read_float(version_.curr_pos_z, z)) {
        last_error_ = "failed to read position floats";
        return false;
    }
    // 1.12.x engine reports Y as X internally; normalise to logical (x,y,z).
    // Neither 1.12 profile has a usable orientation offset (pos_r == 0),
    // so orientation is always left unset here -- matches the AutoIt
    // original, which always returns an empty r for these versions.
    if (version_.is_vanilla()) {
        out = Position{static_cast<double>(y), static_cast<double>(x), static_cast<double>(z),
                       std::nullopt};
    } else {
        out = Position{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z),
                       std::nullopt};
    }
    return true;
}

bool TeleportService::write_position(const Position& pos) {
    if (version_.is_pointer_chain()) {
        uintptr_t base;
        if (!resolve_player_base(base)) return false;
        // 3.3.5 also swaps axes on write.
        bool ok = backend_.write_float(base + version_.pos_x, static_cast<float>(pos.y)) &&
                  backend_.write_float(base + version_.pos_y, static_cast<float>(pos.x)) &&
                  backend_.write_float(base + version_.pos_z, static_cast<float>(pos.z));
        // Orientation is only written when the point actually requests one
        // (a missing value means "preserve current facing") and only on
        // clients that expose pos_r at all.
        if (ok && version_.pos_r && pos.orientation.has_value()) {
            ok = backend_.write_float(base + version_.pos_r, static_cast<float>(*pos.orientation));
        }
        if (!ok) last_error_ = "failed to write position/orientation floats";
        return ok;
    }

    uintptr_t ax, ay, az;
    if (!resolve_vanilla_write_addresses(ax, ay, az)) return false;
    // Vanilla layout: (x_target=engine_x, y_target=engine_y); UI passes
    // (y, x, z) just like WritePosition($y, $x, $z) in the AutoIt original.
    bool ok = backend_.write_float(ax, static_cast<float>(pos.y)) &&
              backend_.write_float(ay, static_cast<float>(pos.x)) &&
              backend_.write_float(az, static_cast<float>(pos.z));
    if (!ok) last_error_ = "failed to write position floats";
    return ok;
}

bool TeleportService::teleport_step(const Position& target, const StepConfig& cfg,
                                     int& steps_out,
                                     const std::function<void(double)>& sleeper) {
    steps_out = 0;
    Position current;
    if (!read_position(current)) return false;

    double distance = current.distance_to(target);
    double per_step = cfg.distance_per_step > 1e-6 ? cfg.distance_per_step : 1e-6;
    int steps = static_cast<int>(std::floor(distance / per_step)) + 1;
    if (steps < 1) steps = 1;

    double dx = (target.x - current.x) / steps;
    double dy = (target.y - current.y) / steps;
    double dz = (target.z - current.z) / steps;

    Position cursor = current;
    // Don't force a facing change on every intermediate step -- apply the
    // target's orientation once, together with the final position, so the
    // walk is smooth and only "snaps" to face the requested direction (if
    // any) at the very end.
    cursor.orientation.reset();
    for (int i = 0; i < steps; ++i) {
        cursor.x += dx;
        cursor.y += dy;
        cursor.z += dz;
        if (i == steps - 1) cursor.orientation = target.orientation;
        if (!write_position(cursor)) return false;
        if (cfg.sleep_between_steps_sec > 0) {
            if (sleeper) {
                sleeper(cfg.sleep_between_steps_sec);
            } else {
                Sleep(static_cast<DWORD>(cfg.sleep_between_steps_sec * 1000.0));
            }
        }
    }
    steps_out = steps;
    return true;
}

// ---------------------------------------------------------------------
// TeleportStepSession

void TeleportStepSession::start(TeleportService& svc, const Position& target,
                                 const StepConfig& cfg) {
    Position current;
    if (!svc.read_position(current)) {
        active_ = false;
        return;
    }
    double distance = current.distance_to(target);
    double per_step = cfg.distance_per_step > 1e-6 ? cfg.distance_per_step : 1e-6;
    int steps = static_cast<int>(std::floor(distance / per_step)) + 1;
    if (steps < 1) steps = 1;

    delta_.x = (target.x - current.x) / steps;
    delta_.y = (target.y - current.y) / steps;
    delta_.z = (target.z - current.z) / steps;
    cursor_ = current;
    cursor_.orientation.reset(); // applied once, on the final tick only
    target_orientation_ = target.orientation;
    steps_remaining_ = steps;
    interval_sec_ = cfg.sleep_between_steps_sec > 0 ? cfg.sleep_between_steps_sec : 0.0;
    accum_sec_ = interval_sec_; // fire the first step immediately
    active_ = true;

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    last_tick_qpc_ = qpc.QuadPart;
}

bool TeleportStepSession::tick(TeleportService& svc) {
    if (!active_) return false;

    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    double elapsed = static_cast<double>(now.QuadPart - last_tick_qpc_) /
                      static_cast<double>(freq.QuadPart);
    last_tick_qpc_ = now.QuadPart;
    accum_sec_ += elapsed;

    while (accum_sec_ >= interval_sec_ && steps_remaining_ > 0) {
        cursor_.x += delta_.x;
        cursor_.y += delta_.y;
        cursor_.z += delta_.z;
        if (steps_remaining_ == 1) cursor_.orientation = target_orientation_;
        svc.write_position(cursor_); // best-effort; errors surface via last_error()
        --steps_remaining_;
        accum_sec_ -= interval_sec_;
        if (interval_sec_ <= 0.0) break; // no throttling requested, drain in one go
    }

    if (steps_remaining_ <= 0) {
        active_ = false;
    }
    return active_;
}

bool TeleportService::read_map_id(uint32_t& out) {
    if (version_.map_id == 0) {
        last_error_ = "map_id not supported on this client";
        return false;
    }
    if (!backend_.read_uint32(version_.map_id, out)) {
        last_error_ = "failed to read map_id";
        return false;
    }
    return true;
}

} // namespace th
