#include "core/game_teleport_api.h"
#include "services/teleport_api.h"

#include <wincrypt.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {
constexpr uintptr_t kImageBase = 0x400000;
constexpr uintptr_t kRunScript = 0x7070a0;
constexpr uintptr_t kLuaState = 0xcf3694;
constexpr std::array<BYTE, 32> kExecutableHash = {
    0x43,0x37,0x1a,0x05,0xd6,0x62,0x84,0x26,0x8b,0xb3,0xf8,0xb0,0x63,0xfa,0x50,0xfc,
    0xbb,0xe4,0xd4,0xe6,0xb5,0x9e,0x59,0xe5,0x9b,0x44,0xcd,0x81,0x8c,0x13,0xa1,0x77};
constexpr BYTE kRunScriptBytes[] = {0x56,0x6a,0x00,0x8b,0xf1,0x52,0x56,0xe8};
SRWLOCK lock = SRWLOCK_INIT;
std::string pending;
UINT dispatch_message = 0;

bool matches_client() {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase) return false;
    wchar_t path[32768];
    DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!len || len >= std::size(path)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    BYTE buffer[65536];
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        if (!CryptHashData(hash, buffer, count, 0)) { ok = false; break; }
    }
    std::array<BYTE, 32> digest{};
    DWORD size = static_cast<DWORD>(digest.size());
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &size, 0) &&
                 size == digest.size() && digest == kExecutableHash;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    BYTE code[sizeof(kRunScriptBytes)]{};
    SIZE_T read = 0;
    return ok && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(kRunScript),
                                   code, sizeof(code), &read) && read == sizeof(code) &&
           std::memcmp(code, kRunScriptBytes, sizeof(code)) == 0;
}
}

bool queue_game_teleport_api(HWND hwnd, const std::string& command, std::string& error) {
    if (!hwnd || !IsWindow(hwnd) || !matches_client()) {
        error = "unsupported or modified WoW.exe (requires verified 1.12.3 build 6141)";
        return false;
    }
    if (command.size() > 180 || command.compare(0, 18, ".synthopen tp xyz ") != 0 ||
        command.find_first_not_of(".abcdefghijklmnopqrstuvwxyz 0123456789-") != std::string::npos) {
        error = "invalid API command";
        return false;
    }
    UINT message = RegisterWindowMessageW(L"TeleportHackDX.TeleportApi.1");
    if (!message) { error = "cannot register API dispatch message"; return false; }
    AcquireSRWLockExclusive(&lock);
    if (!pending.empty()) {
        ReleaseSRWLockExclusive(&lock);
        error = "another API request is waiting";
        return false;
    }
    dispatch_message = message;
    pending = command;
    bool posted = PostMessageW(hwnd, message, 0, 0) != 0;
    if (!posted) pending.clear();
    ReleaseSRWLockExclusive(&lock);
    if (!posted) error = "could not post API request to game window";
    return posted;
}

bool handle_game_teleport_api_message(UINT message) {
    AcquireSRWLockExclusive(&lock);
    if (!dispatch_message || message != dispatch_message) {
        ReleaseSRWLockExclusive(&lock);
        return false;
    }
    std::string command;
    command.swap(pending);
    ReleaseSRWLockExclusive(&lock);
    if (command.empty()) return true;
    uintptr_t state = 0;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(kLuaState),
                           &state, sizeof(state), &read) || read != sizeof(state) || !state) return true;
    // Lua 5.0 event arguments are globals on the 1.12 client. The server's
    // response is displayed in game chat, not misreported as local success.
    const std::string script = th::build_teleport_api_script(command);
    using Execute = void (__fastcall *)(const char*, const char*);
    reinterpret_cast<Execute>(kRunScript)(script.c_str(), "TeleportHackDXApi");
    return true;
}

void shutdown_game_teleport_api() {
    AcquireSRWLockExclusive(&lock);
    pending.clear();
    dispatch_message = 0;
    ReleaseSRWLockExclusive(&lock);
}
