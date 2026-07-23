// Core value objects for the teleport-hack domain.
//
// Ported from TeleportHackOnVanilla/win/src/teleport_hack/domain/models.py.
// Pure data + light helpers, no platform / I/O dependencies.
#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace th {

inline const std::string ALL_CATEGORY = "\xe6\x89\x80\xe6\x9c\x89";   // "所有"
inline const std::string OTHER_CATEGORY = "\xe5\x85\xb6\xe4\xbb\x96"; // "其他"

struct Position {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

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
