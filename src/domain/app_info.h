// Build version + release notes for the "关于" tab, shared by every UI
// (ImGui DLL/EXE through th::App, WinForms/WPF through the C ABI). Mirrors
// TeleportHackOnVanilla's about.au3 (author + per-tag release notes).
#pragma once

#include <string>

namespace th {

// CI passes the artifact version (e.g. "0.1.3rc", or the tag itself) via
// -DTH_BUILD_VERSION=...; local builds report "dev".
const std::string& build_version();

const std::string& app_author();

// UTF-8, '\n' line endings, newest release first.
const std::string& release_notes();

} // namespace th
