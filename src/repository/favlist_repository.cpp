#include "repository/favlist_repository.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace th {

namespace {

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        if (current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }
    // Trim trailing blank lines (mirrors FavlistRepository.load in Python).
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        size_t idx = 0;
        out = std::stod(s, &idx);
        return idx == s.size();
    } catch (...) {
        return false;
    }
}

// Map IDs are unsigned dwords written by TeleportHackOnVanilla's ReadMap();
// anything else (sign, spaces, fractions, overflow) is malformed.
bool parse_map_id(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 10) return false;
    uint64_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    if (value > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

} // namespace

bool FavlistRepository::load(std::vector<TeleportPoint>& out, std::string& error) const {
    out.clear();
    if (!std::filesystem::exists(path_)) return true; // missing file => empty list

    std::ifstream fh(path_, std::ios::binary);
    if (!fh) {
        error = "cannot open " + path_ + " for reading";
        return false;
    }
    std::ostringstream ss;
    ss << fh.rdbuf();
    std::vector<std::string> lines = split_lines(ss.str());

    constexpr size_t old_column_count = 4; // desc, x, y, z
    constexpr size_t new_column_count = 5; // desc, x, y, z, orientation
    constexpr size_t map_column_count = 6; // desc, x, y, z, orientation, map id
    size_t i = 0, n = lines.size();
    while (i < n) {
        std::vector<std::string> parts = split(lines[i], sep_);
        if (parts.size() >= old_column_count && parts.size() <= map_column_count) {
            double x, y, z;
            bool coords_ok = parse_double(parts[1], x) && parse_double(parts[2], y) &&
                              parse_double(parts[3], z);
            bool optional_ok = true;
            std::optional<double> orientation;
            if (coords_ok && parts.size() >= new_column_count && !parts[4].empty()) {
                double r;
                if (parse_double(parts[4], r)) {
                    orientation = r;
                } else {
                    optional_ok = false; // malformed orientation: skip like a bad number
                }
            }
            std::optional<uint32_t> map_id;
            if (coords_ok && parts.size() == map_column_count && !parts[5].empty()) {
                uint32_t map;
                if (parse_map_id(parts[5], map)) {
                    map_id = map;
                } else {
                    optional_ok = false; // malformed map id: skip the record too
                }
            }
            if (coords_ok && optional_ok) {
                Position pos{x, y, z, orientation};
                pos.map_id = map_id;
                out.push_back(TeleportPoint{parts[0], pos});
            }
            ++i;
        } else if (i + old_column_count - 1 < n) {
            // Legacy multi-line record: desc / x / y / z, optionally
            // followed by a 5th orientation line (empty or numeric). Peek
            // at line i+4 to decide whether it belongs to this record or
            // is the next record's description.
            double x, y, z;
            if (parse_double(lines[i + 1], x) && parse_double(lines[i + 2], y) &&
                parse_double(lines[i + 3], z)) {
                std::optional<double> orientation;
                size_t consumed = old_column_count;
                if (i + new_column_count - 1 < n) {
                    const std::string& maybe_r = lines[i + new_column_count - 1];
                    double r;
                    if (maybe_r.empty()) {
                        consumed = new_column_count;
                    } else if (parse_double(maybe_r, r)) {
                        orientation = r;
                        consumed = new_column_count;
                    }
                    // else: line i+4 is the next record's description;
                    // leave consumed at old_column_count.
                }
                out.push_back(TeleportPoint{lines[i], Position{x, y, z, orientation}});
                i += consumed;
            } else {
                ++i; // malformed legacy candidate; keep scanning
            }
        } else {
            break; // trailing junk
        }
    }
    return true;
}

int FavlistRepository::save(const std::vector<TeleportPoint>& points, std::string& error) const {
    std::filesystem::path p(path_);
    std::error_code ec;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::filesystem::path temporary = p;
    temporary += ".tmp";
    std::ofstream fh(temporary, std::ios::binary | std::ios::trunc);
    if (!fh) {
        error = "cannot open " + temporary.string() + " for writing";
        return -1;
    }
    int count = 0;
    for (const auto& pt : points) {
        fh << pt.description << sep_ << format_float(pt.position.x) << sep_
           << format_float(pt.position.y) << sep_ << format_float(pt.position.z) << sep_;
        if (pt.position.orientation.has_value()) {
            fh << format_float(*pt.position.orientation);
        }
        // The 6th map field is only emitted when known, so files without
        // map IDs keep their canonical 5-field lines byte for byte.
        if (pt.position.map_id.has_value()) {
            fh << sep_ << *pt.position.map_id;
        }
        fh << "\n";
        ++count;
    }
    fh.flush();
    fh.close();
    if (!fh) {
        error = "failed while writing " + temporary.string();
        std::filesystem::remove(temporary, ec);
        return -1;
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), p.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "cannot replace " + path_ + " (GetLastError=" +
                std::to_string(GetLastError()) + ")";
        std::filesystem::remove(temporary, ec);
        return -1;
    }
#else
    std::filesystem::rename(temporary, p, ec);
    if (ec) {
        error = "cannot replace " + path_ + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return -1;
    }
#endif
    return count;
}

} // namespace th
