#include "util/paths.h"

#include <Windows.h>

namespace th {

namespace {
std::string compute_dll_directory() {
    HMODULE hModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&compute_dll_directory), &hModule);

    char buf[MAX_PATH] = {0};
    if (hModule) {
        GetModuleFileNameA(hModule, buf, MAX_PATH);
    }
    std::string path(buf);
    if (path.empty()) return ".\\";

    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return ".\\";
    return path.substr(0, slash + 1);
}
} // namespace

const std::string& dll_directory() {
    static const std::string dir = compute_dll_directory();
    return dir;
}

std::string resolve_path(const std::string& relative) {
    if (relative.size() >= 2 && relative[1] == ':') return relative;       // "C:\..."
    if (relative.size() >= 2 && relative[0] == '\\' && relative[1] == '\\')
        return relative; // UNC "\\server\share"
    return dll_directory() + relative;
}

} // namespace th
