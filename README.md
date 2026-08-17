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
  1.12.x): the existing global speed field (`0x814`), plus a separately
  toggleable movement-speed port from The Hitchhiker's Hack that continuously
  writes run (`0x81C`) and swim (`0x824`) speed and restores `7.0`/`4.7` when
  disabled. Both injected and desktop modes share this feature.
- Import / merge (dedup by description+x+y+z) / replace / export favourites
- Shared application settings persisted to `settings.json` next to the
  DLL/EXE. The same file holds separate DLL and desktop ImGui layout
  snapshots, including favourites-table column widths, so the modes don't
  overwrite each other's layout.
- Chinese IME input in the injected ImGui text fields, with IME context and
  message ownership isolated from WoW's own chat input
- Process picker ("WoW 进程": list/refresh/attach) -- desktop build only,
  since the DLL build always operates on its own host process. The desktop
  toolbar also has a persisted `Always on top` toggle for its native window.
- Two additional .NET 8/x86 desktop UIs:
  `TeleportHackDesktopWinForms.exe` and `TeleportHackDesktopWpf.exe`. Both
  call the same C++ business services through `TeleportHackCApi.dll` and
  provide attach/version selection, category/search, favourite CRUD +
  save/reload/import/export, Teleport.lua update/combined insert, global
  hotkeys, teleport, four patch toggles, global/movement speed, logs,
  always-on-top, and shared settings.json persistence. They require the
  .NET 8 x86 Desktop Runtime.
- `TeleportHackLauncher.exe`: reads `wow_exe_path` from the shared
  `settings.json`, starts that Win32 `WoW.exe`, injects the sibling
  `TeleportHackDX.dll` once process initialization permits remote threads,
  and reports configuration or injection errors in a console window

**Extra, not present in TeleportHackOnVanilla**:

- "读取到炉石" (read-to-hearthstone) button: like Edit, reads the
  character's current x/y/z/orientation, but instead of updating our own
  favlist.fav it patches the matching entry (matched by exact description
  text) directly inside a server-side `Teleport.lua` script's
  `local FAV = { {"name", mapId, x, y, z, o}, ... }` table -- a surgical
  single-line edit, not a full regeneration, so everything else in that
  file (menu code, other entries, mapId) is left byte-for-byte untouched
  (including its UTF-8 BOM and CRLF line endings). Configure the path via
  Settings ("Teleport.lua path"); empty by default since most setups
  don't have this file. See `src/repository/teleport_lua_repository.h`.
- "插后 表&炉石" button: performs the normal insert-after operation and
  inserts the same new row immediately after the selected entry in
  `Teleport.lua`. The new row uses the description input plus the current
  3.3.5 character's mapId/x/y/z/orientation. Like ordinary insert-after, the
  in-memory favlist change still requires Save to persist to `favlist.fav`.
  Both Lua operations parse the live FAV table and locate the selected name
  (plus its duplicate occurrence), so unrelated Lua edits or reordering do
  not require identical row indexes with `favlist.fav`.

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
  launcher/main.cpp       Starts configured WoW.exe and injects TeleportHackDX.dll
  capi/                    Stable extern-C facade used by C# desktop UIs
  domain/                 Position/TeleportPoint/Category/HotkeyBinding + per-version memory layout
  memory/                 MemoryBackend interface + InProcessBackend (SEH-guarded) + OutOfProcessBackend (RPM/WPM)
  services/               TeleportService, FeatureService, FavouritesService, HotkeyService, ...
  repository/             favlist.fav / hotkey.txt / settings.json / Teleport.lua readers-writers
  ui/                     App: builds the ImGui frame every render -- shared by both executables
  util/                   DLL/EXE-relative path resolution, Toolhelp32 process enumeration
vendor/
  imgui/                  vendored Dear ImGui (core + dx9/win32 backends)
  minhook/                vendored MinHook (32-bit subset, TeleportHackDX.dll only)
tests/
  orientation_tests.cpp           favlist.fav format + 3.3.5 pos_r compatibility tests (CTest)
  teleport_lua_repository_tests.cpp  Teleport.lua single-entry edit tests, incl. against the real file (CTest)
  settings_repository_tests.cpp   shared settings + separate DLL/desktop ImGui layout tests (CTest)
csharp/
  TeleportHackCore/               P/Invoke declarations + managed session wrapper
  TeleportHackDesktopWinForms/    .NET 8 WinForms attach UI
  TeleportHackDesktopWpf/         .NET 8 WPF attach UI
favlist.fav, hotkey.txt   starter data copied from TeleportHackOnVanilla
使用说明.html              full Chinese walkthrough (build/inject/desktop-mode/troubleshooting)
```

`CMakeLists.txt` separates `th_business` (attach-mode domain/services and
repositories, no ImGui/D3D) from `th_core` (ImGui App and injection-only
helpers). `TeleportHackCApi.dll` statically links only `th_business`, while
the injected DLL and ImGui EXE link `th_core`; adding C# UIs therefore does
not alter the injection DLL's runtime dependency graph.

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

Native output: `TeleportHackDX.dll`, `TeleportHackDesktop.exe`,
`TeleportHackLauncher.exe`, and `TeleportHackCApi.dll`. CI additionally runs
`dotnet publish -r win-x86 --no-self-contained` for
`TeleportHackDesktopWinForms.exe` and `TeleportHackDesktopWpf.exe`.

No legacy DirectX SDK (June 2010) is required -- only `d3d9.h`/`d3d9.lib`
from the standard Windows SDK, which ships with Visual Studio. CI
(`.github/workflows/build.yml`) builds all native and managed targets on
`windows-latest` on every
push.

## Usage

1. Keep the downloaded artifact files together. In particular, both C# EXEs
   require the sibling `TeleportHackCore.dll`, `TeleportHackCApi.dll`, their
   `.deps.json`/`.runtimeconfig.json` files, and the installed .NET 8 x86
   Desktop Runtime. The native binaries resolve data files from their own
   directory rather than from `WoW.exe`'s working directory.
2. For one-click launch, set `wow_exe_path` in `settings.json` (or through
   the Settings window), then double-click `TeleportHackLauncher.exe`.
   It starts WoW and injects the sibling DLL automatically.
3. **Manual injected mode**: start WoW, then inject `TeleportHackDX.dll` with your preferred
   injector (Cheat Engine, Extreme Injector, Xenos, ...) -- see
   `使用说明.html` for a step-by-step Cheat Engine walkthrough. **Win +
   Insert** toggles the overlay.
   **Desktop mode**: run `TeleportHackDesktop.exe` (ImGui),
   `TeleportHackDesktopWinForms.exe`, or `TeleportHackDesktopWpf.exe`; pick
   the running `WoW.exe` and click Attach.
4. Pick your client version from the dropdown at the top the first time
   (or set `default_version` in `settings.json`).
5. WPF and WinForms load the shared favlist/hotkey/Teleport.lua paths and
   common values from `settings.json`, while keeping their own window bounds
   and topmost state in separate `winforms_*` / `wpf_*` fields. Managed saves
   preserve native ImGui layout fields byte-for-byte at the JSON property
   level.

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
  `/W4 /permissive-` and zero warnings from our own code, and runs the
  `tests/` suite via CTest. The DLL has also
  been injected into a live WoW client and confirmed working end-to-end
  (overlay renders, teleport functions).
