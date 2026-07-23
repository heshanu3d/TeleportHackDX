#include "memory/memory_backend.h"

#include <Windows.h>
#include <cstring>

namespace th {

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

} // namespace th
