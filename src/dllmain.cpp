#include <Windows.h>

#include "core/hook.h"

namespace {
DWORD WINAPI InitializeHookThread(LPVOID) {
    InitializeHook();
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            if (HANDLE thread = CreateThread(nullptr, 0, InitializeHookThread, nullptr, 0, nullptr))
                CloseHandle(thread);
            break;
        case DLL_PROCESS_DETACH:
            CleanupHook();
            break;
        default:
            break;
    }
    return TRUE;
}
