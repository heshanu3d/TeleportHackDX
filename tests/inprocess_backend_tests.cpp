#include "memory/memory_backend.h"
#include <Windows.h>
#include <cstdint>
#include <iostream>

int main() {
    th::InProcessBackend backend;
    int failures = 0;
    auto check = [&](bool result, const char* message) {
        if (!result) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const size_t page = info.dwPageSize;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2,
                                                       MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    float original = 12.5f;
    const auto address = reinterpret_cast<uintptr_t>(memory);
    check(backend.write_float(address, original), "write valid float");
    float value = 0;
    check(backend.read_float(address, value) && value == original, "read valid float");
    check(backend.write_byte(address + sizeof(float), 0x7a), "write valid byte");
    uint8_t byte = 0;
    check(backend.read_byte(address + sizeof(float), byte) && byte == 0x7a, "read valid byte");
    check(!backend.read_float(0, value), "null read is caught");
    check(!backend.write_float(0, 3.0f), "null write is caught");
    DWORD old = 0;
    check(VirtualProtect(memory + page, page, PAGE_NOACCESS, &old) != 0, "protect second page");
    uint8_t buffer[8]{};
    check(!backend.read_bytes(address + page - 4, buffer, sizeof(buffer)),
          "cross-page invalid read is caught");
    check(!backend.write_byte(address + page, 1), "no-access write is caught");
    check(VirtualProtect(memory + page, page, PAGE_READONLY, &old) != 0, "set read-only page");
    check(!backend.write_byte(address + page, 1), "read-only write is rejected");
    check(!backend.write_float(address + page - 2, 1.0f), "cross-page read-only write is rejected");
    check(VirtualFree(memory, 0, MEM_RELEASE) != 0, "free test pages");
    if (failures) return 1;
    std::cout << "in-process SEH tests passed\n";
    return 0;
}
