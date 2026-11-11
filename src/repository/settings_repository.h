// User-tweakable application settings, persisted as small JSON file next
// to the DLL (portable layout, mirrors the Python port's settings.json).
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/settings.py
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "domain/models.h"
#include "domain/category_groups.h"

namespace th {

constexpr int kMinCrudButtonWidth = 64;
constexpr int kMaxCrudButtonWidth = 240;
constexpr int kDefaultCrudButtonWidth = 96;
constexpr int kMinCrudButtonHeight = 30;
constexpr int kMaxCrudButtonHeight = 80;
constexpr int kDefaultCrudButtonHeight = 36;
constexpr int kMinCrudButtonSpacing = 0;
constexpr int kMaxCrudButtonSpacing = 32;
constexpr int kDefaultCrudButtonSpacing = 6;
constexpr int kMinInterfaceOpacity = 30;
constexpr int kMaxInterfaceOpacity = 100;
constexpr int kMinFavouriteRows = 4;
constexpr int kMaxFavouriteRows = 40;

struct Settings {
    double step_distance = 10.0;
    int step_sleep_ms = 40;
    std::string default_version = "3.3.5";
    std::string favlist_path = "favlist.fav";
    std::string hotkey_path = "hotkey.txt";
    // WoW.exe launched by TeleportHackLauncher.exe. Empty until explicitly
    // configured; relative paths resolve from the launcher directory.
    std::string wow_exe_path;
    // Server-side Teleport.lua script (see repository/teleport_lua_repository.h)
    // used by the "读取到炉石" and "插后 表&炉石" buttons to update/insert the
    // current position in its `local FAV` table. Empty by default -- most
    // setups don't have this file next to the DLL/EXE, so configure it explicitly.
    std::string teleport_lua_path = "";
    std::string last_category = ALL_CATEGORY;
    bool category_groups_customized = false;
    std::vector<CategoryGroup> customized_category_groups;
    std::vector<std::string> search_history;
    bool independent_search = false;
    std::string global_search;
    std::map<std::string, std::string> category_searches;
    double speed_value = 7.0;
    double movement_speed_value = 7.0;
    bool fast_step = false;
    bool desktop_always_on_top = false;
    // Shared by the injected and standalone ImGui interfaces.
    bool auto_save_favourites = false;
    bool teleport_api_enabled = false;
    bool right_click_teleport = false;
    int collapse_hotkey = 0; // Win32 virtual key, 0 disables the shortcut.
    bool collapse_hotkey_ctrl = false;
    bool collapse_hotkey_shift = false;
    bool collapse_hotkey_alt = false;
    int interface_opacity = 100;
    int favourite_list_rows = 13;
    // Column order: x, y, z, r, map.
    std::array<bool, 5> show_favourite_columns = {true, true, true, true, true};
    std::string imgui_theme = "dark";
    int crud_button_width = kDefaultCrudButtonWidth;
    int crud_button_height = kDefaultCrudButtonHeight;
    int crud_button_spacing = kDefaultCrudButtonSpacing;
    // Desktop (Attach) UIs re-check the running WoW.exe processes every N
    // seconds and attach automatically when exactly one has a new PID
    // (TeleportHackOnVanilla's "WoW PID 查询间隔", 1..3600, default 30).
    int pid_poll_seconds = 30;

    // Dear ImGui's native ini-format layout snapshots, stored as escaped
    // JSON strings so both modes keep independent window/table layouts
    // without creating extra files.
    std::string dll_imgui_layout;
    std::string desktop_imgui_layout;

    // Standalone desktop build's native OS window bounds. The injected
    // overlay's bounds are part of dll_imgui_layout above.
    // -1 for x/y means "not set yet, let the platform pick a default".
    int window_x = -1;
    int window_y = -1;
    int window_width = 500;
    int window_height = 900;

    // Managed desktop UI geometry. Native saves include these fields so
    // opening/closing an ImGui build does not erase WPF/WinForms state.
    int winforms_window_x = -1;
    int winforms_window_y = -1;
    int winforms_window_width = 820;
    int winforms_window_height = 820;
    bool winforms_window_saved = false;
    bool winforms_always_on_top = false;
    double wpf_window_x = -1.0;
    double wpf_window_y = -1.0;
    double wpf_window_width = 820.0;
    double wpf_window_height = 740.0;
    bool wpf_window_saved = false;
    bool wpf_always_on_top = false;
};

class SettingsRepository {
public:
    explicit SettingsRepository(std::filesystem::path path) : path_(std::move(path)) {}

    const std::filesystem::path& path() const { return path_; }

    // Forgiving: missing file, corrupt JSON, or unknown keys all yield a
    // valid Settings{} instead of an error.
    Settings load() const;

    bool save(const Settings& settings, std::string& error) const;

private:
    std::filesystem::path path_;
};

constexpr int kMinPidPollSeconds = 1;
constexpr int kMaxPidPollSeconds = 3600;
constexpr int kDefaultPidPollSeconds = 30;

// ParseWowPidPollSeconds(): accepts 1..3600, anything else -> `fallback`.
int sanitize_pid_poll_seconds(int seconds, int fallback = kDefaultPidPollSeconds);
int sanitize_collapse_hotkey(int key);

} // namespace th
