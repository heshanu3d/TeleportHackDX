// Resolve paths relative to this DLL's own directory. Since the DLL is
// injected into WoW.exe, there's no meaningful "current working directory"
// to rely on -- everything (settings.json, favlist.fav, hotkey.txt) lives
// next to the DLL itself, mirroring how the original AutoIt/portable
// Python builds kept their data files next to the executable.
#pragma once

#include <string>

namespace th {

// Absolute path to the directory containing this DLL, with a trailing
// backslash. Falls back to ".\\" if it can't be determined.
const std::string& dll_directory();

// Joins dll_directory() with `relative` unless `relative` is already an
// absolute path (starts with a drive letter or UNC prefix).
std::string resolve_path(const std::string& relative);

} // namespace th
