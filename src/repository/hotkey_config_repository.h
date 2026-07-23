// Read the simple hotkey.txt config file.
//
// Format:
//   # comment line (any line starting with '#')
//   ^1
//   斯坦索姆-入口
//   ^2
//   ...
// Pairs of (combo, point-name) lines. Comment/blank lines are ignored.
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/infrastructure/repository/hotkey_config.py
#pragma once

#include <string>
#include <vector>

#include "domain/models.h"

namespace th {

class HotkeyConfigRepository {
public:
    explicit HotkeyConfigRepository(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }

    // Missing file yields an empty list (not an error).
    bool load(std::vector<HotkeyBinding>& out, std::string& error) const;

private:
    std::string path_;
};

} // namespace th
