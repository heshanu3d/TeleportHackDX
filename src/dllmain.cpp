#include <Windows.h>

#include "core/hook.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            CreateThread(nullptr, 0,
                         reinterpret_cast<LPTHREAD_START_ROUTINE>(InitializeHook), nullptr, 0,
                         nullptr);
            break;
        case DLL_PROCESS_DETACH:
            CleanupHook();
            break;
        default:
            break;
    }
    return TRUE;
}
