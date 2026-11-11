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
- Tabbed layout matching TeleportHackOnVanilla's AutoIt window: **坐标点**
  (list, 最近使用的坐标, CRUD, Teleport), **分类导航** (categories grouped
  into 杂项 / 东部王国 / 卡利姆多 / 燃烧的远征 / 巫妖王之怒, three per row;
  clicking one jumps back to 坐标点), **设置** (step teleport, patch
  toggles, speeds, PID polling, paths, import/export) and **关于**
  (version, author, release notes). The log stays visible below the tabs.
- Optional per-point orientation/facing (5th `favlist.fav` field, radians)
  on all three clients: 3.3.5 uses `player_base + 0x7A8`; 1.12.1/1.12.3 use
  the float right after the destination Z of the movement struct
  (`dst_z_offsets` chain + 4, TeleportHackOnVanilla `ReadFacingVanilla`).
  An empty field means "preserve current facing" -- distinct from an
  explicit `0`. Step-teleport applies the target facing once, on the final
  step, rather than every intermediate step.
- Optional per-point map ID (6th `favlist.fav` field:
  `描述#x#y#z#r#mapid`), read from each client's `MapID` dword (1.12.1
  `0x00B4E378`, 1.12.3 `0x00B52A20`, 3.3.5 `0x00AB63BC`). Add/insert/edit
  record the current map automatically. Teleporting to a point whose map
  differs from the current one -- or whose map can't be verified -- is
  cancelled ("cross-map teleport is not supported"), for list, hotkey and
  recent teleports alike. Points without a map ID are never checked. Old
  4/5-field files keep loading unchanged; `save()` writes the 5-field form
  and appends the map field only for points that have one.
- 1.12.3 reads the current x/y in the same order as the AutoIt original
  (no swap; 1.12.1 still swaps), so positions recorded with DX on 1.12.3
  teleport back to the same spot.
- 最近使用的坐标: the last 20 teleport destinations (deduplicated by
  x/y/z + map, newest first); double-click to teleport there again.
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
- Import / merge (dedup by description + x/y/z + facing + map) / replace / export favourites
- Shared application settings persisted to `settings.json` next to the
  DLL/EXE. The same file holds separate DLL and desktop ImGui layout
  snapshots, including favourites-table column widths, so the modes don't
  overwrite each other's layout.
- Chinese IME input in the injected ImGui text fields, with IME context and
  message ownership isolated from WoW's own chat input
- Process picker ("WoW 进程": list/refresh/attach) -- desktop build only,
  since the DLL build always operates on its own host process. When exactly
  one `WoW.exe` is running the desktop UIs attach to it automatically (at
  startup, on Refresh, and every `pid_poll_seconds`, 1-3600 s, default 30,
  configurable on the 设置 tab), so a restarted client is picked up again.
  The desktop toolbar also has a persisted `Always on top` toggle for its
  native window.
- Two additional .NET 8/x86 desktop UIs:
  `TeleportHackDesktopWinForms.exe` and `TeleportHackDesktopWpf.exe`. Both
  call the same C++ business services through `TeleportHackCApi.dll` and
  provide the same tabs (坐标点 / 分类导航 / 设置 / 关于), attach/version
  selection with single-process auto-attach + PID polling, category/search,
  favourite CRUD (with facing + map) + save/reload/import/export,
  最近使用的坐标, the cross-map guard, Teleport.lua update/combined insert,
  global hotkeys, four patch toggles, global/movement speed, logs,
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
  the 设置 tab ("Teleport.lua path"); empty by default since most setups
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
  sync_tests.cpp                  TeleportHackOnVanilla sync: map field, 1.12.x facing/read order,
                                  cross-map guard, recent teleports, navigation groups (CTest)
  capi_tests.cpp                  C ABI smoke tests (CTest)
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

For a local 32-bit LLVM-MinGW build, run **pwsh -File .\build-local.ps1**.
The script locates the toolchain under D:\SDK\llvm-mingw, or accepts
-ToolchainRoot / LLVM_MINGW_ROOT, builds and runs all CTest tests, then creates
a new timestamped package under build\local-mingw. Existing data files in
previous packages are not overwritten. The MinGW x86 build uses Windows
process-memory APIs for fault protection; MSVC retains its SEH implementation.

To install an existing local package into the ignored project-root dist folder,
run **powershell -ExecutionPolicy Bypass -File .\install-local.ps1**. The
installer selects the latest local release by default; pass -PackagePath to
select a particular release. Existing favourites, hotkeys, settings and
unrelated files are preserved. Replaced program files and server scripts are
backed up inside dist.

To build, test and install the exact newly generated package in one step, run
**powershell -ExecutionPolicy Bypass -File .\build_and_install-local.ps1**.
Both scripts accept -DestinationPath for a different installation directory;
the combined script also accepts -ToolchainRoot and -Jobs.

The standard MSVC build requires Visual Studio 2019+ and CMake 3.15+.
Both build variants **must be built 32-bit**
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
2. For one-click launch, set `wow_exe_path` in `settings.json` (or on the
   设置 tab), then double-click `TeleportHackLauncher.exe`.
   It starts WoW and injects the sibling DLL automatically.
3. **Manual injected mode**: start WoW, then inject `TeleportHackDX.dll` with your preferred
   injector (Cheat Engine, Extreme Injector, Xenos, ...) -- see
   `使用说明.html` for a step-by-step Cheat Engine walkthrough. **Win +
   Insert** toggles the overlay.
   **Desktop mode**: run `TeleportHackDesktop.exe` (ImGui),
   `TeleportHackDesktopWinForms.exe`, or `TeleportHackDesktopWpf.exe`. With
   a single running `WoW.exe` they attach automatically; otherwise pick the
   process and click Attach.
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
- The version shown on the 关于 tab is the CI artifact version
  (`-DTH_BUILD_VERSION=...`, e.g. `0.1.3rc`); local builds show `dev`.
- Build-verified: CI builds both targets on `windows-latest` with
  `/W4 /permissive-` and zero warnings from our own code, and runs the
  `tests/` suite via CTest. The DLL has also
  been injected into a live WoW client and confirmed working end-to-end
  (overlay renders, teleport functions).
