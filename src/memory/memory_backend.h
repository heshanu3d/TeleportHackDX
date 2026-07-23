// Backend abstraction for reading/writing "process" memory.
//
// Mirrors infrastructure/memory/backend.py's MemoryBackend protocol. Unlike
// the Windows/pymem Python port -- which attaches to a *different* WoW.exe
// process via ReadProcessMemory/WriteProcessMemory -- this DLL is injected
// directly into the game process, so all addresses are dereferenced
// in-process. Every access is guarded with SEH so a bad pointer chain
// (e.g. player object not yet loaded) logs a soft failure instead of
// crashing the host game.
//
// NOTE: all absolute addresses in domain::GameVersion assume the game
// executable is loaded at its default (preferred) image base, exactly like
// the original AutoIt/Python tools assumed when reading via
// ReadProcessMemory with literal addresses. These WoW builds are not
// ASLR-enabled, so this holds in practice.
#pragma once

#include <cstdint>
#include <string>

namespace th {

class MemoryBackend {
public:
    virtual ~MemoryBackend() = default;

    virtual bool is_attached() const = 0;

    virtual bool read_float(uintptr_t address, float& out) = 0;
    virtual bool read_uint32(uintptr_t address, uint32_t& out) = 0;
    virtual bool read_byte(uintptr_t address, uint8_t& out) = 0;
    virtual bool read_pointer(uintptr_t address, uintptr_t& out) = 0;
    virtual bool read_bytes(uintptr_t address, void* buffer, size_t length) = 0;

    virtual bool write_float(uintptr_t address, float value) = 0;
    virtual bool write_byte(uintptr_t address, uint8_t value) = 0;
};

// The only backend we ship: dereferences pointers within the current
// process, guarded by SEH.
class InProcessBackend final : public MemoryBackend {
public:
    bool is_attached() const override { return true; }

    bool read_float(uintptr_t address, float& out) override;
    bool read_uint32(uintptr_t address, uint32_t& out) override;
    bool read_byte(uintptr_t address, uint8_t& out) override;
    bool read_pointer(uintptr_t address, uintptr_t& out) override;
    bool read_bytes(uintptr_t address, void* buffer, size_t length) override;

    bool write_float(uintptr_t address, float value) override;
    bool write_byte(uintptr_t address, uint8_t value) override;
};

} // namespace th
