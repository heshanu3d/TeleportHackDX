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
    std::string last_category = ALL_CATEGORY;
    double speed_value = 7.0;
    bool fast_step = false;
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
