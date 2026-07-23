// Cross-process memory backend used by the standalone desktop build
// (TeleportHackDesktop): reads/writes another process's memory via
// OpenProcess + ReadProcessMemory/WriteProcessMemory, the same way the
// original AutoIt/Python tools operated. This lets the exact same
// services/UI code (App, TeleportService, FeatureService, ...) be
// exercised without injecting a DLL -- attach to a running WoW.exe from
// an ordinary desktop window instead.
#pragma once

#include <Windows.h>

#include "memory/memory_backend.h"

namespace th {

class OutOfProcessBackend final : public MemoryBackend {
public:
    OutOfProcessBackend() = default;
    ~OutOfProcessBackend() override { detach(); }

    OutOfProcessBackend(const OutOfProcessBackend&) = delete;
    OutOfProcessBackend& operator=(const OutOfProcessBackend&) = delete;

    // Closes any existing handle and opens `pid`. Returns false (and
    // leaves the backend detached) if OpenProcess fails.
    bool attach(unsigned long pid);
    void detach();

    unsigned long pid() const { return pid_; }
    bool is_attached() const override { return process_ != nullptr; }

    bool read_float(uintptr_t address, float& out) override;
    bool read_uint32(uintptr_t address, uint32_t& out) override;
    bool read_byte(uintptr_t address, uint8_t& out) override;
    bool read_pointer(uintptr_t address, uintptr_t& out) override;
    bool read_bytes(uintptr_t address, void* buffer, size_t length) override;

    bool write_float(uintptr_t address, float value) override;
    bool write_byte(uintptr_t address, uint8_t value) override;

private:
    HANDLE process_ = nullptr;
    unsigned long pid_ = 0;

    bool read_raw(uintptr_t address, void* buffer, size_t length);
    bool write_raw(uintptr_t address, const void* buffer, size_t length);
};

} // namespace th
