#include "util/process_list.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace th {

namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

std::vector<ProcessEntry> list_processes_by_name(const std::string& executable) {
    std::vector<ProcessEntry> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    std::string wanted = to_lower(executable);
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    if (Process32First(snap, &entry)) {
        do {
            if (to_lower(entry.szExeFile) == wanted) {
                result.push_back(ProcessEntry{entry.th32ProcessID, entry.szExeFile});
            }
        } while (Process32Next(snap, &entry));
    }
    CloseHandle(snap);
    return result;
}

std::string read_player_name(MemoryBackend& backend, uintptr_t address, size_t length) {
    if (address == 0) return "";
    std::vector<uint8_t> buf(length, 0);
    if (!backend.read_bytes(address, buf.data(), length)) return "";

    bool all_zero = true;
    size_t nul_pos = length;
    for (size_t i = 0; i < length; ++i) {
        if (buf[i] != 0) all_zero = false;
        if (buf[i] == 0 && nul_pos == length) nul_pos = i;
    }
    if (all_zero) return "";
    return std::string(reinterpret_cast<char*>(buf.data()), nul_pos);
}

} // namespace th
