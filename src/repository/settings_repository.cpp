#include "repository/settings_repository.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "services/search_history.h"

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

int sanitize_pid_poll_seconds(int seconds, int fallback) {
    return (seconds >= kMinPidPollSeconds && seconds <= kMaxPidPollSeconds) ? seconds : fallback;
}

int sanitize_collapse_hotkey(int key) {
    if ((key >= 0x30 && key <= 0x39) || (key >= 0x41 && key <= 0x5a) ||
        (key >= 0x70 && key <= 0x7b) || (key >= 0x21 && key <= 0x24) ||
        key == 0x2d || key == 0x2e) return key;
    return 0;
}

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
    get_str("wow_exe_path", s.wow_exe_path);
    get_str("teleport_lua_path", s.teleport_lua_path);
    get_str("last_category", s.last_category);
    get_bool("category_groups_customized", s.category_groups_customized);
    if (s.category_groups_customized) {
        const auto count_value = [&](const std::string& key, int& count) {
            const auto it = map.find(key);
            if (it == map.end()) return false;
            try {
                size_t consumed = 0;
                const long long value = std::stoll(it->second, &consumed);
                if (consumed != it->second.size() || value < 0 || value > INT_MAX ||
                    static_cast<unsigned long long>(value) > map.size()) return false;
                count = static_cast<int>(value);
                return true;
            } catch (...) { return false; }
        };
        int group_count = 0;
        bool valid = count_value("category_groups_count", group_count) && group_count > 0;
        std::vector<CategoryGroup> groups;
        for (int i = 0; valid && i < group_count; ++i) {
            const std::string prefix = "category_group_" + std::to_string(i);
            const auto name = map.find(prefix + "_name");
            int item_count = 0;
            if (name == map.end() || !count_value(prefix + "_count", item_count)) { valid = false; break; }
            CategoryGroup group{name->second, {}};
            for (int j = 0; j < item_count; ++j) {
                const auto item = map.find(prefix + "_item_" + std::to_string(j));
                if (item == map.end()) { valid = false; break; }
                group.categories.push_back(item->second);
            }
            groups.push_back(std::move(group));
        }
        if (valid && valid_category_group_config(groups)) s.customized_category_groups = std::move(groups);
        else s.category_groups_customized = false;
    }
    get_bool("independent_search", s.independent_search);
    get_str("global_search", s.global_search);
    if (!valid_search_query(s.global_search)) s.global_search.clear();
    int category_search_count = 0;
    get_int("category_search_count", category_search_count);
    category_search_count = std::clamp(category_search_count, 0, static_cast<int>(std::min(map.size(), static_cast<size_t>(INT_MAX))));
    for (int i = 0; i < category_search_count; ++i) {
        const auto key = map.find("category_search_name_" + std::to_string(i));
        const auto value = map.find("category_search_value_" + std::to_string(i));
        if (key != map.end() && !key->second.empty() && value != map.end() &&
            !value->second.empty() && valid_search_query(value->second))
            s.category_searches.emplace(key->second, value->second);
    }
    int history_count = 0;
    get_int("search_history_count", history_count);
    history_count = std::clamp(history_count, 0, static_cast<int>(std::min(map.size(), static_cast<size_t>(INT_MAX))));
    for (int i = 0; i < history_count; ++i) {
        const auto it = map.find("search_history_" + std::to_string(i));
        if (it != map.end() && valid_search_history_entry(it->second) &&
            std::find(s.search_history.begin(), s.search_history.end(), it->second) == s.search_history.end())
            s.search_history.push_back(it->second);
    }
    get_double("speed_value", s.speed_value);
    get_double("movement_speed_value", s.movement_speed_value);
    get_bool("fast_step", s.fast_step);
    get_bool("desktop_always_on_top", s.desktop_always_on_top);
    get_bool("auto_save_favourites", s.auto_save_favourites);
    get_bool("teleport_api_enabled", s.teleport_api_enabled);
    get_bool("right_click_teleport", s.right_click_teleport);
    get_int("collapse_hotkey", s.collapse_hotkey);
    s.collapse_hotkey = sanitize_collapse_hotkey(s.collapse_hotkey);
    get_bool("collapse_hotkey_ctrl", s.collapse_hotkey_ctrl);
    get_bool("collapse_hotkey_shift", s.collapse_hotkey_shift);
    get_bool("collapse_hotkey_alt", s.collapse_hotkey_alt);
    get_int("interface_opacity", s.interface_opacity);
    get_int("favourite_list_rows", s.favourite_list_rows);
    bool legacy_show_coordinates = true;
    get_bool("show_favourite_coordinates", legacy_show_coordinates);
    s.show_favourite_columns.fill(legacy_show_coordinates);
    const char* column_keys[] = {"show_favourite_x", "show_favourite_y", "show_favourite_z",
                                 "show_favourite_r", "show_favourite_map"};
    for (size_t i = 0; i < s.show_favourite_columns.size(); ++i)
        get_bool(column_keys[i], s.show_favourite_columns[i]);
    s.interface_opacity = std::clamp(s.interface_opacity, kMinInterfaceOpacity, kMaxInterfaceOpacity);
    s.favourite_list_rows = std::clamp(s.favourite_list_rows, kMinFavouriteRows, kMaxFavouriteRows);
    get_str("imgui_theme", s.imgui_theme);
    if (s.imgui_theme != "light") s.imgui_theme = "dark";
    get_int("crud_button_width", s.crud_button_width);
    get_int("crud_button_height", s.crud_button_height);
    get_int("crud_button_spacing", s.crud_button_spacing);
    s.crud_button_width = std::clamp(s.crud_button_width, kMinCrudButtonWidth, kMaxCrudButtonWidth);
    s.crud_button_height = std::clamp(s.crud_button_height, kMinCrudButtonHeight, kMaxCrudButtonHeight);
    s.crud_button_spacing = std::clamp(s.crud_button_spacing, kMinCrudButtonSpacing, kMaxCrudButtonSpacing);
    get_int("pid_poll_seconds", s.pid_poll_seconds);
    s.pid_poll_seconds = sanitize_pid_poll_seconds(s.pid_poll_seconds);
    get_str("dll_imgui_layout", s.dll_imgui_layout);
    get_str("desktop_imgui_layout", s.desktop_imgui_layout);
    get_int("window_x", s.window_x);
    get_int("window_y", s.window_y);
    get_int("window_width", s.window_width);
    get_int("window_height", s.window_height);
    get_int("winforms_window_x", s.winforms_window_x);
    get_int("winforms_window_y", s.winforms_window_y);
    get_int("winforms_window_width", s.winforms_window_width);
    get_int("winforms_window_height", s.winforms_window_height);
    get_bool("winforms_window_saved", s.winforms_window_saved);
    get_bool("winforms_always_on_top", s.winforms_always_on_top);
    get_double("wpf_window_x", s.wpf_window_x);
    get_double("wpf_window_y", s.wpf_window_y);
    get_double("wpf_window_width", s.wpf_window_width);
    get_double("wpf_window_height", s.wpf_window_height);
    get_bool("wpf_window_saved", s.wpf_window_saved);
    get_bool("wpf_always_on_top", s.wpf_always_on_top);
    return s;
}

bool SettingsRepository::save(const Settings& settings, std::string& error) const {
    if (settings.category_groups_customized && !valid_category_group_config(settings.customized_category_groups)) {
        error = "invalid category group configuration";
        return false;
    }
    std::filesystem::path p(path_);
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream fh(path_, std::ios::binary | std::ios::trunc);
    if (!fh) {
        error = "cannot open " + path_.u8string() + " for writing";
        return false;
    }
    fh << "{\n";
    fh << "  \"step_distance\": " << settings.step_distance << ",\n";
    fh << "  \"step_sleep_ms\": " << settings.step_sleep_ms << ",\n";
    fh << "  \"default_version\": \"" << json_escape(settings.default_version) << "\",\n";
    fh << "  \"favlist_path\": \"" << json_escape(settings.favlist_path) << "\",\n";
    fh << "  \"hotkey_path\": \"" << json_escape(settings.hotkey_path) << "\",\n";
    fh << "  \"wow_exe_path\": \"" << json_escape(settings.wow_exe_path) << "\",\n";
    fh << "  \"teleport_lua_path\": \"" << json_escape(settings.teleport_lua_path) << "\",\n";
    fh << "  \"last_category\": \"" << json_escape(settings.last_category) << "\",\n";
    fh << "  \"category_groups_customized\": " << (settings.category_groups_customized ? "true" : "false") << ",\n";
    if (settings.category_groups_customized) {
        fh << "  \"category_groups_count\": " << settings.customized_category_groups.size() << ",\n";
        for (size_t i = 0; i < settings.customized_category_groups.size(); ++i) {
            const auto& group = settings.customized_category_groups[i];
            const std::string prefix = "category_group_" + std::to_string(i);
            fh << "  \"" << prefix << "_name\": \"" << json_escape(group.name) << "\",\n";
            fh << "  \"" << prefix << "_count\": " << group.categories.size() << ",\n";
            for (size_t j = 0; j < group.categories.size(); ++j)
                fh << "  \"" << prefix << "_item_" << j << "\": \"" << json_escape(group.categories[j]) << "\",\n";
        }
    }
    fh << "  \"independent_search\": " << (settings.independent_search ? "true" : "false") << ",\n";
    fh << "  \"global_search\": \"" << json_escape(settings.global_search) << "\",\n";
    fh << "  \"category_search_count\": " << settings.category_searches.size() << ",\n";
    size_t category_index = 0;
    for (const auto& entry : settings.category_searches) {
        fh << "  \"category_search_name_" << category_index << "\": \"" << json_escape(entry.first) << "\",\n";
        fh << "  \"category_search_value_" << category_index << "\": \"" << json_escape(entry.second) << "\",\n";
        ++category_index;
    }
    fh << "  \"search_history_count\": " << settings.search_history.size() << ",\n";
    for (size_t i = 0; i < settings.search_history.size(); ++i)
        fh << "  \"search_history_" << i << "\": \"" << json_escape(settings.search_history[i]) << "\",\n";
    fh << "  \"speed_value\": " << settings.speed_value << ",\n";
    fh << "  \"movement_speed_value\": " << settings.movement_speed_value << ",\n";
    fh << "  \"fast_step\": " << (settings.fast_step ? "true" : "false") << ",\n";
    fh << "  \"auto_save_favourites\": " << (settings.auto_save_favourites ? "true" : "false") << ",\n";
    fh << "  \"teleport_api_enabled\": " << (settings.teleport_api_enabled ? "true" : "false") << ",\n";
    fh << "  \"right_click_teleport\": " << (settings.right_click_teleport ? "true" : "false") << ",\n";
    fh << "  \"collapse_hotkey\": " << sanitize_collapse_hotkey(settings.collapse_hotkey) << ",\n";
    fh << "  \"collapse_hotkey_ctrl\": " << (settings.collapse_hotkey_ctrl ? "true" : "false") << ",\n";
    fh << "  \"collapse_hotkey_shift\": " << (settings.collapse_hotkey_shift ? "true" : "false") << ",\n";
    fh << "  \"collapse_hotkey_alt\": " << (settings.collapse_hotkey_alt ? "true" : "false") << ",\n";
    fh << "  \"interface_opacity\": " << std::clamp(settings.interface_opacity, kMinInterfaceOpacity, kMaxInterfaceOpacity) << ",\n";
    fh << "  \"favourite_list_rows\": " << std::clamp(settings.favourite_list_rows, kMinFavouriteRows, kMaxFavouriteRows) << ",\n";
    const char* column_keys[] = {"show_favourite_x", "show_favourite_y", "show_favourite_z",
                                 "show_favourite_r", "show_favourite_map"};
    for (size_t i = 0; i < settings.show_favourite_columns.size(); ++i)
        fh << "  \"" << column_keys[i] << "\": " << (settings.show_favourite_columns[i] ? "true" : "false") << ",\n";
    fh << "  \"imgui_theme\": \"" << (settings.imgui_theme == "light" ? "light" : "dark") << "\",\n";
    fh << "  \"crud_button_width\": " << std::clamp(settings.crud_button_width, kMinCrudButtonWidth, kMaxCrudButtonWidth) << ",\n";
    fh << "  \"crud_button_height\": " << std::clamp(settings.crud_button_height, kMinCrudButtonHeight, kMaxCrudButtonHeight) << ",\n";
    fh << "  \"crud_button_spacing\": " << std::clamp(settings.crud_button_spacing, kMinCrudButtonSpacing, kMaxCrudButtonSpacing) << ",\n";
    fh << "  \"desktop_always_on_top\": "
       << (settings.desktop_always_on_top ? "true" : "false") << ",\n";
    fh << "  \"pid_poll_seconds\": " << sanitize_pid_poll_seconds(settings.pid_poll_seconds)
       << ",\n";
    fh << "  \"dll_imgui_layout\": \"" << json_escape(settings.dll_imgui_layout) << "\",\n";
    fh << "  \"desktop_imgui_layout\": \"" << json_escape(settings.desktop_imgui_layout)
       << "\",\n";
    fh << "  \"window_x\": " << settings.window_x << ",\n";
    fh << "  \"window_y\": " << settings.window_y << ",\n";
    fh << "  \"window_width\": " << settings.window_width << ",\n";
    fh << "  \"window_height\": " << settings.window_height << ",\n";
    fh << "  \"winforms_window_x\": " << settings.winforms_window_x << ",\n";
    fh << "  \"winforms_window_y\": " << settings.winforms_window_y << ",\n";
    fh << "  \"winforms_window_width\": " << settings.winforms_window_width << ",\n";
    fh << "  \"winforms_window_height\": " << settings.winforms_window_height << ",\n";
    fh << "  \"winforms_window_saved\": "
       << (settings.winforms_window_saved ? "true" : "false") << ",\n";
    fh << "  \"winforms_always_on_top\": "
       << (settings.winforms_always_on_top ? "true" : "false") << ",\n";
    fh << "  \"wpf_window_x\": " << settings.wpf_window_x << ",\n";
    fh << "  \"wpf_window_y\": " << settings.wpf_window_y << ",\n";
    fh << "  \"wpf_window_width\": " << settings.wpf_window_width << ",\n";
    fh << "  \"wpf_window_height\": " << settings.wpf_window_height << ",\n";
    fh << "  \"wpf_window_saved\": " << (settings.wpf_window_saved ? "true" : "false")
       << ",\n";
    fh << "  \"wpf_always_on_top\": " << (settings.wpf_always_on_top ? "true" : "false")
       << "\n";
    fh << "}\n";
    fh.flush();
    if (!fh) {
        error = "failed while writing " + path_.u8string();
        return false;
    }
    fh.close();
    if (!fh) {
        error = "failed while closing " + path_.u8string();
        return false;
    }
    return true;
}

} // namespace th
