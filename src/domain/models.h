// Core value objects for the teleport-hack domain.
//
// Ported from TeleportHackOnVanilla/win/src/teleport_hack/domain/models.py.
// Pure data + light helpers, no platform / I/O dependencies.
#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace th {

inline const std::string ALL_CATEGORY = "\xe6\x89\x80\xe6\x9c\x89";   // "所有"
inline const std::string OTHER_CATEGORY = "\xe5\x85\xb6\xe4\xbb\x96"; // "其他"

struct Position {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    // Optional facing/orientation in radians. 3.3.5 stores it at
    // player_base + pos_r (0x7A8); 1.12.1/1.12.3 store it right after the
    // destination Z float of the movement struct (see
    // GameVersion::dst_r_offset). A missing value means "preserve the
    // player's current facing" -- this is NOT the same as an explicit
    // orientation of 0, so the distinction must survive
    // parsing/serialisation/dedup/writes.
    std::optional<double> orientation;

    // Optional WoW map ID (0 = Eastern Kingdoms, 1 = Kalimdor, 530 =
    // Outland, 571 = Northrend; instances have their own IDs). Missing means
    // "unknown, don't verify"; an explicit 0 is a real map. Writing
    // coordinates can't move the player to another map, so teleports to a
    // point that carries a map ID are cancelled unless the player is
    // currently on that map (TeleportHackOnVanilla's CanTeleportToMap).
    std::optional<uint32_t> map_id;

    double distance_to(const Position& other) const {
        double dx = x - other.x, dy = y - other.y, dz = z - other.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

// Formats like the AutoIt/Python original: integers print without a
// decimal point, otherwise up to 6 decimals with trailing zeros trimmed.
std::string format_float(double value);

struct TeleportPoint {
    std::string description;
    Position position;

    // Derived: substring before the first '-', else OTHER_CATEGORY.
    std::string category() const;
};

struct Category {
    std::string name;
    std::vector<TeleportPoint> points;
};

// Bucket a flat point list into ordered Category objects (insertion order
// of first appearance, mirrors points_to_category_dict in models.py).
std::vector<Category> points_to_categories(const std::vector<TeleportPoint>& points);

enum class Modifier {
    Shift, // '+'
    Ctrl,  // '^'
    Alt,   // '!'
};

struct HotkeyBinding {
    std::string raw_combo; // e.g. "^1", "!a"
    std::string point_name;

    // Parsed modifier set + trailing key token (case-insensitive single char
    // expected, matches AutoIt convention).
    std::vector<Modifier> modifiers() const;
    std::string key() const;
};

} // namespace th
