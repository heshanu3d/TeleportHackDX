# TeleportHackDX

An in-game ImGui overlay for World of Warcraft **1.12.1 / 1.12.3 / 3.3.5**
that fully replaces the AutoIt-based [`TeleportHackOnVanilla`](../TeleportHackOnVanilla)
tool. Two executables share the same UI/service source code (see
`src/ui/app.h` for why this is possible):

- **`TeleportHackDX.dll`** -- inject this into `WoW.exe`; it hooks
  Direct3D9's `EndScene`/`Reset` (via [MinHook](https://github.com/TsudaKageyu/minhook))
  and draws a [Dear ImGui](https://github.com/ocornut/imgui) window on top
  of the game, following the same hooking pattern as
  [AzDeltaQQ/WoW3.3.5aDirectXHookWithGui](https://github.com/AzDeltaQQ/WoW3.3.5aDirectXHookWithGui).
  Operates on the process it's injected into (`BackendMode::InProcess`).
- **`TeleportHackDesktop.exe`** -- an ordinary standalone window (own D3D9
  device, no injection) that attaches to a separately-running `WoW.exe`
  over `ReadProcessMemory`/`WriteProcessMemory` (`BackendMode::Attach`),
  the same way the original AutoIt/Python tools worked. Useful for
  iterating on/verifying the UI, favlist, and hotkey logic without
  re-injecting for every change.

See [`使用说明.html`](使用说明.html) for a full (Chinese) walkthrough:
building, injecting via Cheat Engine/other injectors, using the desktop
attach mode, and troubleshooting.

## Feature parity with TeleportHackOnVanilla

Ported (see `win/docs/design.md` in `TeleportHackOnVanilla` for the memory
layout rationale this is based on):

- Favourites list (`favlist.fav`), category filter (derived from the
  `desc` prefix before `-`), description search, insert/append/edit/delete/add
- Optional per-point orientation/facing (5th `favlist.fav` field, radians;
  3.3.5 only). An empty field means "preserve current facing" -- distinct
  from an explicit `0`. Old 4-field files keep loading unchanged; `save()`
  always writes the canonical 5-field form. Step-teleport applies the
  target facing once, on the final step, rather than every intermediate
  step.
- Instant teleport + smooth "step" teleport (non-blocking; advances once
  per rendered frame instead of `Sleep()`-blocking the game thread)
- Hotkey bindings (`hotkey.txt`, `^`/`!`/`+` modifier syntax) via Win32
  `RegisterHotKey` -- works even while the overlay is hidden, matching the
  original `HotKeySet()` behaviour
- Byte-patch toggles: AntiJump, AutoLoot, PatchLoot, LuaUnlock
- Speed hack (3.3.5 only -- the client field this writes doesn't exist on
  1.12.x)
- Import / merge (dedup by description+x+y+z) / replace / export favourites
- Settings persisted to `settings.json` next to the DLL/EXE
- Process picker ("WoW 进程": list/refresh/attach) -- desktop build only,
  since the DLL build always operates on its own host process

**Not ported** (out of scope, see project discussion): the AutoIt
`bot/` scripts (hardcoded-account auto-login, dungeon farm bots) and
multi-client "sync-tp". These aren't "teleport hack" functionality and
the login script embeds specific account credentials.

## Repo layout

```
CMakeLists.txt
src/
  dllmain.cpp            DLL entry point (TeleportHackDX.dll only)
  core/hook.cpp           D3D9 EndScene/Reset hook, WndProc hook (TeleportHackDX.dll only)
  desktop/main.cpp        Standalone Win32 + D3D9 window/message loop (TeleportHackDesktop.exe only)
  domain/                 Position/TeleportPoint/Category/HotkeyBinding + per-version memory layout
  memory/                 MemoryBackend interface + InProcessBackend (SEH-guarded) + OutOfProcessBackend (RPM/WPM)
  services/               TeleportService, FeatureService, FavouritesService, HotkeyService, ...
  repository/             favlist.fav / hotkey.txt / settings.json readers-writers
  ui/                     App: builds the ImGui frame every render -- shared by both executables
  util/                   DLL/EXE-relative path resolution, Toolhelp32 process enumeration
vendor/
  imgui/                  vendored Dear ImGui (core + dx9/win32 backends)
  minhook/                vendored MinHook (32-bit subset, TeleportHackDX.dll only)
tests/
  orientation_tests.cpp   favlist.fav format + 3.3.5 pos_r compatibility tests (CTest)
favlist.fav, hotkey.txt   starter data copied from TeleportHackOnVanilla
使用说明.html              full Chinese walkthrough (build/inject/desktop-mode/troubleshooting)
```

`CMakeLists.txt` factors the shared sources into a `th_core` static
library that both `TeleportHackDX` (SHARED) and `TeleportHackDesktop`
(executable) link against.

## Building

Requires Visual Studio 2019+ and CMake 3.15+. **Must be built 32-bit**
(`Win32`) since these WoW clients are 32-bit and every hardcoded address in
`src/domain/versions.cpp` assumes a 32-bit pointer size.

```
mkdir build && cd build
cmake -A Win32 ..
cmake --build . --config Release
ctest -C Release --output-on-failure
```

Output: `build/bin/Release/TeleportHackDX.dll` and
`build/bin/Release/TeleportHackDesktop.exe`.

No legacy DirectX SDK (June 2010) is required -- only `d3d9.h`/`d3d9.lib`
from the standard Windows SDK, which ships with Visual Studio. CI
(`.github/workflows/build.yml`) builds both on `windows-latest` on every
push.

## Usage

1. Copy `TeleportHackDX.dll` (or `TeleportHackDesktop.exe`), `favlist.fav`,
   `hotkey.txt` into the same folder (they can be anywhere; the
   DLL/EXE locates its own directory at runtime and reads/writes
   `favlist.fav` / `hotkey.txt` / `settings.json` next to itself, not next
   to `WoW.exe`).
2. Start the WoW client (1.12.1, 1.12.3, or 3.3.5 — 32-bit).
3. **Injected mode**: inject `TeleportHackDX.dll` with your preferred
   injector (Cheat Engine, Extreme Injector, Xenos, ...) -- see
   `使用说明.html` for a step-by-step Cheat Engine walkthrough. **Win +
   Insert** toggles the overlay.
   **Desktop mode**: just run `TeleportHackDesktop.exe`, pick the running
   `WoW.exe` from the "WoW 进程" list at the top and click Attach.
4. Pick your client version from the dropdown at the top the first time
   (or set `default_version` in `settings.json`).
5. Everything else is identical between both builds and mirrors the
   original tool's layout: category combo, search box, favourites table,
   description input, `step`/AntiJump/AutoLoot/LuaUnlock/PatchLoot toggle
   row, insert/append/edit/delete + add/save/reload rows, the big Teleport
   button, speed row, import/export, and a log pane at the bottom.

## Notes / caveats

- All memory addresses assume the client loads at its default (preferred)
  image base -- true for these builds since they don't use ASLR. This is
  the same assumption the original AutoIt/Python tools made.
- Feature/version offsets are the exact values from
  `TeleportHackOnVanilla/win/src/teleport_hack/domain/versions.py` /
  `feature_toggles.py`; if you're on a differently-patched client build
  the byte-patch toggles may report "unknown byte state" and refuse to
  act rather than write garbage.
- Build-verified: CI builds both targets on `windows-latest` with
  `/W4 /permissive-` and zero warnings from our own code, and runs
  `tests/orientation_tests.cpp` via CTest. The DLL has also
  been injected into a live WoW client and confirmed working end-to-end
  (overlay renders, teleport functions).
