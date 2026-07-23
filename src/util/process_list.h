// Process enumeration + player-name preview for the standalone desktop
// build's "attach to a running WoW.exe" flow. Cross-process equivalent of
// infrastructure/memory/factory.py's list_wow_processes()/read_player_name().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory_backend.h"

namespace th {

struct ProcessEntry {
    unsigned long pid = 0;
    std::string exe_name;
};

// Case-insensitive match against `executable` (e.g. "WoW.exe").
std::vector<ProcessEntry> list_processes_by_name(const std::string& executable);

// Reads up to `length` bytes at `address` via `backend`, stops at the
// first NUL, decodes as UTF-8. Returns an empty string if the address is
// 0, unreadable, or the buffer is all zero bytes.
std::string read_player_name(MemoryBackend& backend, uintptr_t address, size_t length = 12);

} // namespace th
