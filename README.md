# TeleportHackDX

An in-game ImGui overlay for World of Warcraft **1.12.1 / 1.12.3 / 3.3.5**
that fully replaces the AutoIt-based [`TeleportHackOnVanilla`](../TeleportHackOnVanilla)
tool. Instead of an external AutoIt/Python process attaching over
`ReadProcessMemory`, this is a DLL you inject directly into `WoW.exe`; it
hooks Direct3D9's `EndScene`/`Reset` (via [MinHook](https://github.com/TsudaKageyu/minhook))
and draws a [Dear ImGui](https://github.com/ocornut/imgui) window on top of
the game, following the same hooking pattern as
[AzDeltaQQ/WoW3.3.5aDirectXHookWithGui](https://github.com/AzDeltaQQ/WoW3.3.5aDirectXHookWithGui).

## Feature parity with TeleportHackOnVanilla

Ported (see `win/docs/design.md` in `TeleportHackOnVanilla` for the memory
layout rationale this is based on):

- Favourites list (`favlist.fav`), category filter (derived from the
  `desc` prefix before `-`), description search, insert/append/edit/delete/add
- Instant teleport + smooth "step" teleport (non-blocking; advances once
  per rendered frame instead of `Sleep()`-blocking the game thread)
- Hotkey bindings (`hotkey.txt`, `^`/`!`/`+` modifier syntax) via Win32
  `RegisterHotKey` -- works even while the overlay is hidden, matching the
  original `HotKeySet()` behaviour
- Byte-patch toggles: AntiJump, AutoLoot, PatchLoot, LuaUnlock
- Speed hack (3.3.5 only -- the client field this writes doesn't exist on
  1.12.x)
- Import / merge (dedup by description+x+y+z) / replace / export favourites
- Settings persisted to `settings.json` next to the DLL

**Not ported** (out of scope, see project discussion): the AutoIt
`bot/` scripts (hardcoded-account auto-login, dungeon farm bots) and
multi-client "sync-tp". These aren't "teleport hack" functionality and
the login script embeds specific account credentials.

## Repo layout

```
CMakeLists.txt
src/
  dllmain.cpp          DLL entry point
  core/hook.cpp         D3D9 EndScene/Reset hook, WndProc hook, ImGui lifecycle
  domain/               Position/TeleportPoint/Category/HotkeyBinding + per-version memory layout
  memory/               MemoryBackend interface + in-process (SEH-guarded) implementation
  services/             TeleportService, FeatureService, FavouritesService, HotkeyService, ...
  repository/            favlist.fav / hotkey.txt / settings.json readers-writers
  ui/                    App: builds the ImGui frame every render
  util/                  DLL-relative path resolution
vendor/
  imgui/                 vendored Dear ImGui (core + dx9/win32 backends)
  minhook/               vendored MinHook (32-bit subset)
favlist.fav, hotkey.txt   starter data copied from TeleportHackOnVanilla
```

## Building

Requires Visual Studio 2019+ and CMake 3.15+. **Must be built 32-bit**
(`Win32`) since these WoW clients are 32-bit and every hardcoded address in
`src/domain/versions.cpp` assumes a 32-bit pointer size.

```
mkdir build && cd build
cmake -A Win32 ..
cmake --build . --config Release
```

Output DLL: `build/bin/Release/TeleportHackDX.dll`.

No legacy DirectX SDK (June 2010) is required -- only `d3d9.h`/`d3d9.lib`
from the standard Windows SDK, which ships with Visual Studio.

## Usage

1. Copy `TeleportHackDX.dll`, `favlist.fav`, `hotkey.txt` into the same
   folder (they can be anywhere; the DLL locates its own directory at
   runtime and reads/writes `favlist.fav` / `hotkey.txt` / `settings.json`
   next to itself, not next to `WoW.exe`).
2. Start the WoW client (1.12.1, 1.12.3, or 3.3.5 — 32-bit).
3. Inject `TeleportHackDX.dll` with your preferred injector (Cheat Engine,
   Extreme Injector, Xenos, ...).
4. **Win + Insert** toggles the overlay. Pick your client version from the
   dropdown at the top the first time (or set `default_version` in
   `settings.json`).
5. Everything else mirrors the original tool's layout: category combo,
   search box, favourites table, description input, `step`/AntiJump/
   AutoLoot/LuaUnlock/PatchLoot toggle row, insert/append/edit/delete +
   add/save/reload rows, the big Teleport button, speed row, import/export,
   and a log pane at the bottom.

## Notes / caveats

- All memory addresses assume the client loads at its default (preferred)
  image base -- true for these builds since they don't use ASLR. This is
  the same assumption the original AutoIt/Python tools made.
- Feature/version offsets are the exact values from
  `TeleportHackOnVanilla/win/src/teleport_hack/domain/versions.py` /
  `feature_toggles.py`; if you're on a differently-patched client build
  the byte-patch toggles may report "unknown byte state" and refuse to
  act rather than write garbage.
- I don't have a Windows/MSVC/DirectX toolchain available while writing
  this, so it hasn't been build-verified end-to-end. The pure-logic layers
  (domain models, favlist/hotkey/settings parsing, category CRUD, feature
  byte-toggle state machines) were unit-checked with a throwaway harness;
  the Win32/D3D9/ImGui-facing files were syntax-checked against a minimal
  Win32 header shim. Please report the first build error you hit and I'll
  fix it.
