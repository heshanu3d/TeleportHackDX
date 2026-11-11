#include "memory/memory_backend.h"

#include <Windows.h>
#include <cstring>

namespace th {

#if defined(_MSC_VER)
// Every function body here is intentionally free of C++ objects with
// non-trivial destructors, which lets __try/__except live directly inside
// a normal (non /EHa) translation unit.

bool InProcessBackend::read_float(uintptr_t address, float& out) {
    __try {
        out = *reinterpret_cast<volatile float*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InProcessBackend::read_uint32(uintptr_t address, uint32_t& out) {
    __try {
        out = *reinterpret_cast<volatile uint32_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InProcessBackend::read_byte(uintptr_t address, uint8_t& out) {
    __try {
        out = *reinterpret_cast<volatile uint8_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InProcessBackend::read_pointer(uintptr_t address, uintptr_t& out) {
    __try {
        out = *reinterpret_cast<volatile uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InProcessBackend::read_bytes(uintptr_t address, void* buffer, size_t length) {
    __try {
        memcpy(buffer, reinterpret_cast<const void*>(address), length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InProcessBackend::write_float(uintptr_t address, float value) {
    __try {
        *reinterpret_cast<volatile float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InProcessBackend::write_byte(uintptr_t address, uint8_t value) {
    __try {
        *reinterpret_cast<volatile uint8_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

#else
// LLVM-MinGW x86 accepts MS SEH syntax but cannot reliably catch the resulting
// access violation. Use kernel-mediated access rather than dereferencing game
// pointers so invalid addresses never escape into the host process.
namespace {
bool read_current(uintptr_t address, void* buffer, size_t length) {
    SIZE_T transferred = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             buffer, length, &transferred) && transferred == length;
}

bool writable_range(uintptr_t address, size_t length) {
    if (length > UINTPTR_MAX - address) return false;
    const uintptr_t end = address + length;
    while (address < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (info.RegionSize > UINTPTR_MAX - base || base + info.RegionSize <= address) return false;
        address = base + info.RegionSize;
    }
    return true;
}

bool write_current(uintptr_t address, const void* buffer, size_t length) {
    if (!writable_range(address, length)) return false;
    SIZE_T transferred = 0;
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                              buffer, length, &transferred) && transferred == length;
}
}

bool InProcessBackend::read_float(uintptr_t address, float& out) {
    float value = 0;
    if (!read_current(address, &value, sizeof(value))) return false;
    out = value;
    return true;
}
bool InProcessBackend::read_uint32(uintptr_t address, uint32_t& out) {
    uint32_t value = 0;
    if (!read_current(address, &value, sizeof(value))) return false;
    out = value;
    return true;
}
bool InProcessBackend::read_byte(uintptr_t address, uint8_t& out) {
    uint8_t value = 0;
    if (!read_current(address, &value, sizeof(value))) return false;
    out = value;
    return true;
}
bool InProcessBackend::read_pointer(uintptr_t address, uintptr_t& out) {
    uintptr_t value = 0;
    if (!read_current(address, &value, sizeof(value))) return false;
    out = value;
    return true;
}
bool InProcessBackend::read_bytes(uintptr_t address, void* buffer, size_t length) {
    return read_current(address, buffer, length);
}
bool InProcessBackend::write_float(uintptr_t address, float value) {
    return write_current(address, &value, sizeof(value));
}
bool InProcessBackend::write_byte(uintptr_t address, uint8_t value) {
    return write_current(address, &value, sizeof(value));
}
#endif

} // namespace th
