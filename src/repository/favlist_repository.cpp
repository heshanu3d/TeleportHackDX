#include "repository/favlist_repository.h"

#include <filesystem>
#include <fstream>
#include <sstream>

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
    size_t i = 0, n = lines.size();
    while (i < n) {
        std::vector<std::string> parts = split(lines[i], sep_);
        if (parts.size() == old_column_count || parts.size() == new_column_count) {
            double x, y, z;
            bool coords_ok = parse_double(parts[1], x) && parse_double(parts[2], y) &&
                              parse_double(parts[3], z);
            bool orientation_ok = true;
            std::optional<double> orientation;
            if (coords_ok && parts.size() == new_column_count && !parts[4].empty()) {
                double r;
                if (parse_double(parts[4], r)) {
                    orientation = r;
                } else {
                    orientation_ok = false; // malformed orientation: skip like a bad number
                }
            }
            if (coords_ok && orientation_ok) {
                out.push_back(TeleportPoint{parts[0], Position{x, y, z, orientation}});
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
    std::ofstream fh(path_, std::ios::binary | std::ios::trunc);
    if (!fh) {
        error = "cannot open " + path_ + " for writing";
        return -1;
    }
    int count = 0;
    for (const auto& pt : points) {
        fh << pt.description << sep_ << format_float(pt.position.x) << sep_
           << format_float(pt.position.y) << sep_ << format_float(pt.position.z) << sep_;
        if (pt.position.orientation.has_value()) {
            fh << format_float(*pt.position.orientation);
        }
        fh << "\n";
        ++count;
    }
    return count;
}

} // namespace th
