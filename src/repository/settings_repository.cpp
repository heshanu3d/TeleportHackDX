#include "repository/settings_repository.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace th {

namespace {

// ---------------------------------------------------------------------
// A tiny, forgiving JSON-object (flat, one level) reader/writer. Enough
// for the small Settings struct; not a general-purpose JSON library.

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out += static_cast<char>(c);
        }
    }
    return out;
}

// Parses a flat {"key": <string|number|bool>, ...} object into raw text
// values (strings are unescaped; numbers/bools kept as literal tokens).
std::unordered_map<std::string, std::string> parse_flat_object(const std::string& text) {
    std::unordered_map<std::string, std::string> result;
    size_t i = 0, n = text.size();

    auto skip_ws = [&]() {
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r'))
            ++i;
    };
    auto parse_string = [&]() -> std::string {
        std::string out;
        if (i >= n || text[i] != '"') return out;
        ++i;
        while (i < n && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < n) {
                char esc = text[i + 1];
                switch (esc) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    default: out += esc;
                }
                i += 2;
            } else {
                out += text[i];
                ++i;
            }
        }
        if (i < n) ++i; // closing quote
        return out;
    };

    skip_ws();
    if (i >= n || text[i] != '{') return result;
    ++i;
    while (true) {
        skip_ws();
        if (i >= n || text[i] == '}') break;
        if (text[i] != '"') break; // malformed
        std::string key = parse_string();
        skip_ws();
        if (i >= n || text[i] != ':') break;
        ++i;
        skip_ws();
        std::string value;
        if (i < n && text[i] == '"') {
            value = parse_string();
        } else {
            size_t start = i;
            while (i < n && text[i] != ',' && text[i] != '}') ++i;
            value = text.substr(start, i - start);
            // trim whitespace
            size_t a = value.find_first_not_of(" \t\r\n");
            size_t b = value.find_last_not_of(" \t\r\n");
            value = (a == std::string::npos) ? "" : value.substr(a, b - a + 1);
        }
        result[key] = value;
        skip_ws();
        if (i < n && text[i] == ',') {
            ++i;
            continue;
        }
        break;
    }
    return result;
}

} // namespace

Settings SettingsRepository::load() const {
    Settings s;
    if (!std::filesystem::exists(path_)) return s;
    std::ifstream fh(path_, std::ios::binary);
    if (!fh) return s;
    std::ostringstream ss;
    ss << fh.rdbuf();
    auto map = parse_flat_object(ss.str());

    auto get_str = [&](const char* key, std::string& target) {
        auto it = map.find(key);
        if (it != map.end()) target = it->second;
    };
    auto get_double = [&](const char* key, double& target) {
        auto it = map.find(key);
        if (it != map.end()) {
            try {
                target = std::stod(it->second);
            } catch (...) {
            }
        }
    };
    auto get_int = [&](const char* key, int& target) {
        auto it = map.find(key);
        if (it != map.end()) {
            try {
                target = std::stoi(it->second);
            } catch (...) {
            }
        }
    };
    auto get_bool = [&](const char* key, bool& target) {
        auto it = map.find(key);
        if (it != map.end()) target = (it->second == "true");
    };

    get_double("step_distance", s.step_distance);
    get_int("step_sleep_ms", s.step_sleep_ms);
    get_str("default_version", s.default_version);
    get_str("favlist_path", s.favlist_path);
    get_str("hotkey_path", s.hotkey_path);
    get_str("teleport_lua_path", s.teleport_lua_path);
    get_str("last_category", s.last_category);
    get_double("speed_value", s.speed_value);
    get_bool("fast_step", s.fast_step);
    get_int("window_x", s.window_x);
    get_int("window_y", s.window_y);
    get_int("window_width", s.window_width);
    get_int("window_height", s.window_height);
    return s;
}

bool SettingsRepository::save(const Settings& settings, std::string& error) const {
    std::filesystem::path p(path_);
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream fh(path_, std::ios::binary | std::ios::trunc);
    if (!fh) {
        error = "cannot open " + path_ + " for writing";
        return false;
    }
    fh << "{\n";
    fh << "  \"step_distance\": " << settings.step_distance << ",\n";
    fh << "  \"step_sleep_ms\": " << settings.step_sleep_ms << ",\n";
    fh << "  \"default_version\": \"" << json_escape(settings.default_version) << "\",\n";
    fh << "  \"favlist_path\": \"" << json_escape(settings.favlist_path) << "\",\n";
    fh << "  \"hotkey_path\": \"" << json_escape(settings.hotkey_path) << "\",\n";
    fh << "  \"teleport_lua_path\": \"" << json_escape(settings.teleport_lua_path) << "\",\n";
    fh << "  \"last_category\": \"" << json_escape(settings.last_category) << "\",\n";
    fh << "  \"speed_value\": " << settings.speed_value << ",\n";
    fh << "  \"fast_step\": " << (settings.fast_step ? "true" : "false") << ",\n";
    fh << "  \"window_x\": " << settings.window_x << ",\n";
    fh << "  \"window_y\": " << settings.window_y << ",\n";
    fh << "  \"window_width\": " << settings.window_width << ",\n";
    fh << "  \"window_height\": " << settings.window_height << "\n";
    fh << "}\n";
    return true;
}

} // namespace th
