// User-tweakable application settings, persisted as small JSON file next
// to the DLL (portable layout, mirrors the Python port's settings.json).
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/settings.py
#pragma once

#include <string>

#include "domain/models.h"

namespace th {

struct Settings {
    double step_distance = 10.0;
    int step_sleep_ms = 40;
    std::string default_version = "3.3.5";
    std::string favlist_path = "favlist.fav";
    std::string hotkey_path = "hotkey.txt";
    // Server-side Teleport.lua script (see repository/teleport_lua_repository.h)
    // used by the "读取到炉石" button to push the current position/orientation
    // into its `local FAV` table. Empty by default -- most setups don't have
    // this file next to the DLL/EXE, so it must be configured explicitly.
    std::string teleport_lua_path = "";
    std::string last_category = ALL_CATEGORY;
    double speed_value = 7.0;
    bool fast_step = false;

    // Persisted window bounds. In the injected DLL these govern the
    // floating ImGui overlay panel's position/size over the game screen;
    // in the standalone desktop build they govern the actual OS-level
    // window. -1 for x/y means "not set yet, let the platform/ImGui pick
    // a default position".
    int window_x = -1;
    int window_y = -1;
    int window_width = 460;
    int window_height = 820;
};

class SettingsRepository {
public:
    explicit SettingsRepository(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }

    // Forgiving: missing file, corrupt JSON, or unknown keys all yield a
    // valid Settings{} instead of an error.
    Settings load() const;

    bool save(const Settings& settings, std::string& error) const;

private:
    std::string path_;
};

} // namespace th
