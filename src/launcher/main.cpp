#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "repository/settings_repository.h"

namespace {

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                    static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), count);
    return result;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        count, nullptr, nullptr);
    return result;
}

std::filesystem::path launcher_directory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

int fail(const std::string& message) {
    std::cerr << "[error] " << message << "\n\nPress Enter to exit...";
    std::cin.get();
    return 1;
}

std::string win32_error(const char* operation) {
    return std::string(operation) + " failed (GetLastError=" + std::to_string(GetLastError()) +
           ")";
}

bool remote_module_base(DWORD process_id, const std::wstring& module_name, uintptr_t& base,
                        std::string& error) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        HANDLE snapshot =
            CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
        if (snapshot == INVALID_HANDLE_VALUE) {
            const DWORD snapshot_error = GetLastError();
            if (snapshot_error == ERROR_BAD_LENGTH || snapshot_error == ERROR_PARTIAL_COPY) {
                if (attempt == 199) {
                    error = "module snapshot remained unavailable (GetLastError=" +
                            std::to_string(snapshot_error) +
                            "); verify that WoW.exe is a Win32/x86 client";
                    return false;
                }
                Sleep(50);
                continue;
            }
            error = win32_error("CreateToolhelp32Snapshot");
            return false;
        }

        MODULEENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, module_name.c_str()) == 0) {
                    base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                    CloseHandle(snapshot);
                    return true;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        Sleep(50);
    }
    error = "module not found in WoW.exe: " + wide_to_utf8(module_name);
    return false;
}

bool resolve_remote_load_library(DWORD process_id, uintptr_t& address, std::string& error) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
    if (!load_library) {
        error = win32_error("GetProcAddress(LoadLibraryW)");
        return false;
    }

    MEMORY_BASIC_INFORMATION memory = {};
    if (VirtualQuery(reinterpret_cast<const void*>(load_library), &memory, sizeof(memory)) == 0 ||
        !memory.AllocationBase) {
        error = win32_error("VirtualQuery(LoadLibraryW)");
        return false;
    }
    HMODULE local_module = static_cast<HMODULE>(memory.AllocationBase);
    std::wstring local_path(MAX_PATH, L'\0');
    DWORD length =
        GetModuleFileNameW(local_module, local_path.data(), static_cast<DWORD>(local_path.size()));
    if (length == 0 || length >= local_path.size()) {
        error = win32_error("GetModuleFileNameW(LoadLibraryW module)");
        return false;
    }
    local_path.resize(length);

    uintptr_t remote_base = 0;
    const std::wstring module_name = std::filesystem::path(local_path).filename().wstring();
    if (!remote_module_base(process_id, module_name, remote_base, error)) return false;

    const uintptr_t local_base = reinterpret_cast<uintptr_t>(local_module);
    const uintptr_t local_function = reinterpret_cast<uintptr_t>(load_library);
    address = remote_base + (local_function - local_base);
    return true;
}

bool inject_dll(HANDLE process, DWORD process_id, const std::filesystem::path& dll_path,
                std::string& error) {
    const std::wstring dll = dll_path.wstring();
    const SIZE_T byte_count = (dll.size() + 1) * sizeof(wchar_t);
    void* remote_path =
        VirtualAllocEx(process, nullptr, byte_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        error = win32_error("VirtualAllocEx");
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, dll.c_str(), byte_count, &written) ||
        written != byte_count) {
        error = win32_error("WriteProcessMemory");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    uintptr_t remote_load_library = 0;
    if (!resolve_remote_load_library(process_id, remote_load_library, error)) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                       reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_load_library),
                                       remote_path, 0, nullptr);
    if (!thread) {
        error = win32_error("CreateRemoteThread");
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    DWORD wait_result = WaitForSingleObject(thread, 60000);
    DWORD module_handle = 0;
    const bool got_exit_code =
        wait_result == WAIT_OBJECT_0 && GetExitCodeThread(thread, &module_handle);
    bool ok = got_exit_code && module_handle != 0;
    if (!ok) {
        if (wait_result == WAIT_TIMEOUT) {
            error = "DLL injection timed out";
        } else if (got_exit_code) {
            error = "LoadLibraryW returned null; verify that TeleportHackDX.dll is a valid Win32 DLL";
        } else {
            error = win32_error("LoadLibraryW in WoW.exe");
        }
    }
    CloseHandle(thread);
    if (wait_result == WAIT_OBJECT_0) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    }
    if (!ok) return false;

    uintptr_t injected_base = 0;
    return remote_module_base(process_id, dll_path.filename().wstring(), injected_base, error);
}

void terminate_child(PROCESS_INFORMATION& process, std::string& error) {
    if (!TerminateProcess(process.hProcess, 1)) {
        error += "; " + win32_error("TerminateProcess");
    } else if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
        error += "; terminated process did not exit within 5 seconds";
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    process = {};
}

} // namespace

int main() {
#ifdef _WIN32
    static_assert(sizeof(void*) == 4, "TeleportHackLauncher must be built as Win32/x86");
#endif
    SetConsoleOutputCP(CP_UTF8);

    const std::filesystem::path base = launcher_directory();
    if (base.empty()) return fail("cannot determine the launcher directory");

    const std::filesystem::path settings_path = base / L"settings.json";
    const th::SettingsRepository settings_repo(settings_path);
    const th::Settings settings = settings_repo.load();
    if (settings.wow_exe_path.empty()) {
        return fail("configure \"wow_exe_path\" in " + wide_to_utf8(settings_path.wstring()));
    }

    std::wstring configured = utf8_to_wide(settings.wow_exe_path);
    if (configured.empty()) return fail("wow_exe_path is not valid UTF-8");
    if (configured.size() >= 2 && configured.front() == L'"' && configured.back() == L'"') {
        configured = configured.substr(1, configured.size() - 2);
    }

    std::filesystem::path wow_path(configured);
    if (wow_path.is_relative()) wow_path = base / wow_path;
    std::error_code path_error;
    wow_path = std::filesystem::weakly_canonical(wow_path, path_error);
    if (path_error || !std::filesystem::is_regular_file(wow_path, path_error) || path_error) {
        return fail("wow_exe_path does not point to a file: " + wide_to_utf8(wow_path.wstring()));
    }
    const std::wstring wow_filename = wow_path.filename().wstring();
    if (_wcsicmp(wow_filename.c_str(), L"WoW.exe") != 0) {
        return fail("wow_exe_path must point to a file named WoW.exe: " +
                    wide_to_utf8(wow_path.wstring()));
    }

    const std::filesystem::path dll_path = base / L"TeleportHackDX.dll";
    path_error.clear();
    if (!std::filesystem::is_regular_file(dll_path, path_error) || path_error) {
        return fail("TeleportHackDX.dll is missing next to the launcher");
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    const std::wstring wow_executable = wow_path.wstring();
    std::wstring command_line = L"\"" + wow_executable + L"\"";
    const std::wstring working_directory = wow_path.parent_path().wstring();
    if (!CreateProcessW(wow_executable.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, working_directory.c_str(), &startup, &process)) {
        return fail(win32_error("CreateProcessW"));
    }

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        std::string error = win32_error("ResumeThread");
        terminate_child(process, error);
        return fail(error);
    }

    std::string injection_error;
    if (!inject_dll(process.hProcess, process.dwProcessId, dll_path, injection_error)) {
        terminate_child(process, injection_error);
        return fail(injection_error);
    }

    std::cout << "Started " << wide_to_utf8(wow_path.wstring())
              << " and injected TeleportHackDX.dll\n";
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
