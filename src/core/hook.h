#pragma once

#include <Windows.h>
#include <d3d9.h>

// Entry point invoked from DllMain on a fresh thread; sets up MinHook and
// hooks IDirect3DDevice9::EndScene / ::Reset (mirrors AzDeltaQQ's
// WoW3.3.5aDirectXHookWithGui reference project).
void InitializeHook();

// Restores original vtable entries + tears down ImGui. Call before
// FreeLibraryAndExitThread if the DLL supports clean unload.
void CleanupHook();
