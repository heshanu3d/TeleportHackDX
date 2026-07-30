// User-tweakable application settings, persisted as small JSON file next
// to the DLL (portable layout, mirrors the Python port's settings.json).
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/settings.py
#pragma once

#include <filesystem>
#include <string>

#include "domain/models.h"

namespace th {

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
    double speed_value = 7.0;
    bool fast_step = false;

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
    int window_width = 460;
    int window_height = 820;
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

} // namespace th
