#include "memory/outofprocess_backend.h"

#include <cstring>

namespace th {

bool OutOfProcessBackend::attach(unsigned long pid) {
    detach();
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                               PROCESS_QUERY_INFORMATION,
                           FALSE, pid);
    if (!h) return false;
    process_ = h;
    pid_ = pid;
    return true;
}

void OutOfProcessBackend::detach() {
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
    pid_ = 0;
}

bool OutOfProcessBackend::read_raw(uintptr_t address, void* buffer, size_t length) {
    if (!process_) return false;
    SIZE_T read = 0;
    BOOL ok = ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), buffer, length, &read);
    return ok && read == length;
}

bool OutOfProcessBackend::write_raw(uintptr_t address, const void* buffer, size_t length) {
    if (!process_) return false;
    SIZE_T written = 0;
    BOOL ok =
        WriteProcessMemory(process_, reinterpret_cast<LPVOID>(address), buffer, length, &written);
    return ok && written == length;
}

bool OutOfProcessBackend::read_float(uintptr_t address, float& out) {
    return read_raw(address, &out, sizeof(out));
}

bool OutOfProcessBackend::read_uint32(uintptr_t address, uint32_t& out) {
    return read_raw(address, &out, sizeof(out));
}

bool OutOfProcessBackend::read_byte(uintptr_t address, uint8_t& out) {
    return read_raw(address, &out, sizeof(out));
}

bool OutOfProcessBackend::read_pointer(uintptr_t address, uintptr_t& out) {
    // These clients are all 32-bit; a pointer is 4 bytes regardless of the
    // desktop tool's own bitness (build this target 32-bit too, see
    // CMakeLists.txt -- 64-bit couldn't ReadProcessMemory a 32-bit
    // process's memory layout consistently for our purposes anyway).
    uint32_t ptr32 = 0;
    if (!read_raw(address, &ptr32, sizeof(ptr32))) return false;
    out = static_cast<uintptr_t>(ptr32);
    return true;
}

bool OutOfProcessBackend::read_bytes(uintptr_t address, void* buffer, size_t length) {
    return read_raw(address, buffer, length);
}

bool OutOfProcessBackend::write_float(uintptr_t address, float value) {
    return write_raw(address, &value, sizeof(value));
}

bool OutOfProcessBackend::write_byte(uintptr_t address, uint8_t value) {
    return write_raw(address, &value, sizeof(value));
}

} // namespace th
