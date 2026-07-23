# AGENTS.md — TeleportHackDX project memory

This file is a compressed context dump for picking this project back up in
a fresh session. Read this before doing anything else in this repo.

## Interaction rule (inherited from ~/AGENTS.md, keep obeying it here too)

At the end of every assistant turn, call `ask_user` exactly once.
- Provide 2-4 concrete options, also allow free-text input.
- The question must be directly about the current task.
- Do not end the turn with plain text if `ask_user` is available.
- If `ask_user` is unavailable, explicitly say so.

## What this project is

C++/ImGui port of `../TeleportHackOnVanilla` (an AutoIt teleport-hack tool
for WoW 1.12.1/1.12.3/3.3.5), requested so the GUI runs **inside the game**
via a DirectX9 hook instead of an external AutoIt/Python process. Reference
hooking pattern: https://github.com/AzDeltaQQ/WoW3.3.5aDirectXHookWithGui.

Scope decisions made with the user early on (still valid, don't re-litigate
unless asked):
- **Core teleport hack only.** Excluded on purpose: the AutoIt `bot/`
  scripts (hardcoded-credential auto-login, dungeon farm bots) and
  multi-client "sync-tp" — not requested, and the login script embeds real
  account credentials.
- **Manual version dropdown** (1.12.1 / 1.12.3 / 3.3.5), not auto-detection.
- Everything lives in a **new standalone repo**,
  `https://github.com/heshanu3d/TeleportHackDX` (public), sibling to
  `TeleportHackOnVanilla`, not nested inside it.

## Repo / CI / push mechanics (important, non-obvious)

- Remote: `git@github.com:heshanu3d/TeleportHackDX.git`, branch `master`.
- **Push works via SSH** using an already-authorized key already present in
  this sandbox: `~/.ssh/id_ed25519_github` (verified with
  `ssh -T git@github.com` → "Hi heshanu3d!"). The repo's local git config
  already has `core.sshCommand` set to use this key
  (`ssh -o IdentitiesOnly=yes -i ~/.ssh/id_ed25519_github`), so plain
  `git push` just works — no token needed for push.
- **Reading Actions run logs** needs a *separate* GitHub API token (SSH only
  grants git access, not the REST API). The user generated a short-lived
  fine-grained PAT (Actions: Read-only, scoped to this one repo) earlier in
  this session to let me pull failed-build logs via
  `curl -H "Authorization: Bearer $TOKEN" https://api.github.com/...`.
  That token is **not** saved anywhere in the repo (correctly) and has
  likely expired / should not be assumed valid — if you need to read logs
  again, ask the user for a fresh one (same recipe: fine-grained PAT,
  Actions: Read-only, scoped to `heshanu3d/TeleportHackDX`) or ask them to
  paste log text directly from the Actions web UI.
- CI: `.github/workflows/build.yml`, `windows-latest`, builds
  `-A Win32` Release via CMake, uploads a **flat** `dist/` folder (dll +
  exe + favlist.fav + hotkey.txt at the zip root, no `build/bin/Release/`
  nesting) named `TeleportHackDX-<VERSION>-g<shortsha>-win32`. `VERSION`
  file at repo root is the single source of truth, read by both CMake
  (`project(... VERSION ...)`) and the workflow.
- As of commit `610d4c1` (last verified push), CI is green with **zero
  warnings** from our own code under `/W4 /permissive-` (only harmless
  vendored-MinHook warnings existed before I scoped the flags to our own
  target only).

## Verification methodology (no Windows/MSVC available in this sandbox)

Two techniques were used together, repeatedly, for every change:

1. **Real compile+run for the pure-logic layer** (no Windows headers
   involved): domain models, `FavlistRepository`, `FavouritesService`,
   `FavouritesPorter`, `HotkeyConfigRepository`, `SettingsRepository`,
   `FeatureService` (only touches the `MemoryBackend` interface, no direct
   WinAPI calls). Compile with plain `g++ -std=c++17` against a throwaway
   `main()` exercising the logic with real UTF-8/Chinese test data. This
   catches real bugs cheaply and is the highest-value check — **always do
   this for any change to those files.**
2. **Syntax-only check for Windows/D3D9/ImGui-facing files** (`hook.cpp`,
   `dllmain.cpp`, `app.cpp`, `desktop/main.cpp`, `teleport_service.cpp`,
   `inprocess_backend.cpp`, `outofprocess_backend.cpp`, `process_list.cpp`):
   build a minimal hand-written `Windows.h`/`d3d9.h`/`MinHook.h`/
   `imgui_impl_*.h` shim directory and run
   `clang++ -std=c++17 -fsyntax-only -I<shim> -I src ...`. This is *not* a
   real build — it's caught real bugs before (e.g. a `WM_CLOSE`/anonymous
   namespace linkage bug, a `GetWindowRect` on a use-after-destroy handle)
   but can also produce **false confidence** if the shim itself is wrong
   (e.g. my fake `imgui_impl_win32.h` once declared a function that the
   *real* header deliberately omits — CI caught that MSVC error even
   though the Linux shim didn't). **Treat clang-shim "clean" as
   necessary-but-not-sufficient; CI on Windows is the real ground truth.**
   Always push and poll the Actions run to completion before declaring a
   change done. Don't skip this step even for "obviously correct" changes.
3. Do the work in `/tmp/opencode/<scratch-dir>` and delete it when done;
   never leave shim files inside the actual repo.

## Architecture (src/ layout)

```
src/
  dllmain.cpp / core/hook.cpp   DLL-only: MinHook on IDirect3DDevice9::
                                 EndScene/Reset, WndProc hook, Win+Insert
                                 toggle, CJK font load, owns th::App
  desktop/main.cpp               EXE-only: plain Win32 window + own D3D9
                                 device (adapted from Dear ImGui's
                                 example_win32_directx9), same th::App
  domain/                        Position/TeleportPoint/Category/
                                 HotkeyBinding + per-version GameVersion
                                 memory-offset table (1.12.1/1.12.3/3.3.5),
                                 ported verbatim from
                                 TeleportHackOnVanilla/win/.../versions.py
  memory/                        MemoryBackend interface;
                                 InProcessBackend (SEH-guarded direct
                                 pointer deref, DLL use) and
                                 OutOfProcessBackend (OpenProcess +
                                 ReadProcessMemory/WriteProcessMemory,
                                 desktop-attach use)
  services/                      TeleportService (pointer-chain resolution,
                                 axis-swap quirks, step-teleport — both a
                                 blocking variant and a non-blocking
                                 TeleportStepSession driven once/frame),
                                 FeatureService (AntiJump/AutoLoot/
                                 PatchLoot/LuaUnlock byte-patch toggles +
                                 3.3.5 speed hack), FavouritesService +
                                 FavouritesPorter (CRUD/import/export/
                                 merge-dedup), HotkeyService (Win32
                                 RegisterHotKey, not a background thread)
  repository/                    favlist.fav / hotkey.txt / settings.json
                                 readers-writers
  ui/app.{h,cpp}                 th::App — THE SHARED CLASS. Owns every
                                 service, renders the whole ImGui frame.
                                 Used **verbatim** by both executables via
                                 BackendMode::{InProcess, Attach}. Any UI
                                 or favlist/hotkey logic change belongs
                                 here, once, for both builds.
  util/                          paths.cpp (resolve settings/favlist/
                                 hotkey paths relative to the DLL/EXE's
                                 own directory, not cwd — via
                                 GetModuleHandleExA on our own address),
                                 process_list.cpp (Toolhelp32 enumeration +
                                 player-name preview, desktop-attach only)
```

CMake: `th_core` STATIC lib holds everything except `dllmain.cpp` /
`core/hook.cpp` (DLL-only) and `desktop/main.cpp` (EXE-only). Both
`TeleportHackDX` (SHARED) and `TeleportHackDesktop` (executable) link
`th_core`. `th_core` links `imgui` + `comdlg32` as PUBLIC (their symbols
only resolve once a final DLL/EXE links it). `minhook`/`d3d9` are linked
directly on the two final targets, not through `th_core`.

## Key non-obvious design points already baked in

- **Non-blocking step-teleport.** The original AutoIt/Python `Sleep()`
  between steps would freeze the *whole game* since we now run on the
  game's own render thread. `TeleportStepSession::tick()` is instead
  advanced once per rendered frame using `QueryPerformanceCounter`-based
  timing, called from `App::tick()` every frame regardless of overlay
  visibility.
- **Hotkeys via `RegisterHotKey`/`WM_HOTKEY`**, not a background thread —
  works even while the overlay is hidden, forwarded through the hooked/
  owned `WndProc` in both builds via `App::on_wndproc()`.
- **Window bounds persistence** (`Settings::window_x/y/width/height` in
  `settings.json`): in `BackendMode::InProcess` these govern the floating
  ImGui panel (captured every frame after `Begin()`, restored via
  `ImGuiCond_FirstUseEver`); in the desktop build they govern the *actual
  OS window*. Bug already fixed once: don't call `GetWindowRect(hwnd,...)`
  after the message loop drains `WM_QUIT` — by then `DestroyWindow` has
  already invalidated the handle. Track the rect continuously via
  `WM_MOVE`/`WM_SIZE` into a global instead, and use the last-known value
  on exit (see `src/desktop/main.cpp`).
- **All addresses assume default (non-ASLR) image base** — same assumption
  the original AutoIt/Python tools made; no rebase logic implemented, and
  intentionally so (don't add speculative complexity here).
- Favlist/hotkey text is real UTF-8 (verified against actual Chinese data
  in `favlist.fav`) — no GBK/ANSI conversion needed anywhere.

## Status as of commit `610d4c1` (pushed, CI green) — everything below is DONE and verified

1. Full core teleport hack ported (favourites CRUD/category/search,
   instant + step teleport, 4 byte-patch toggles, speed hack, import/
   export/merge, hotkeys, settings).
2. `TeleportHackDX.dll` (inject) and `TeleportHackDesktop.exe` (standalone,
   attach-over-RPM) both share `th::App` verbatim.
3. CI green on `windows-latest`, zero warnings, versioned+flattened
   artifact packaging.
4. Window position/size persistence for both builds (bug-fixed).
5. `README.md` + `使用说明.html` (Chinese walkthrough: build, Cheat Engine
   injection step-by-step, desktop attach-mode usage, troubleshooting) are
   up to date with everything through this point.
6. User manually injected the DLL into a real WoW client and confirmed the
   overlay renders and teleport works end-to-end.

## ✅ DONE — orientation (facing) support

Was reported as pending in an earlier revision of this file (a batch of
patches had been *narrated* in a previous session's transcript but never
actually written to disk -- verified via `grep`/`git status` at the time).
It has since been implemented for real, verified, and pushed. Summary of
what changed and why, in case behaviour needs to be re-derived or extended:

- `domain/models.h`: `Position` gained `std::optional<double> orientation`
  (radians). Missing means "preserve current facing"; this is distinct
  from an explicit `0`, and that distinction is threaded through parsing,
  serialisation, merge-dedup, and memory writes everywhere below.
- `repository/favlist_repository.{h,cpp}`: `load()` accepts old 4-field
  lines (`desc#x#y#z`, no orientation), new 5-field lines (`desc#x#y#z#r`,
  where `r` may be empty), and both the legacy 4-line and 5-line
  multi-line record formats (a heuristic peeks at the would-be 5th line:
  empty or numeric ⇒ belongs to this record and is consumed; anything
  else ⇒ it's the next record's description, and only 4 lines are
  consumed). A malformed orientation field skips just that record
  (mirrors the existing "skip silently" behaviour for bad numbers) rather
  than erroring. `save()` always emits canonical 5-field lines (trailing
  empty field when unset).
- `services/teleport_service.{h,cpp}`: `read_position()` reads `pos_r`
  only when `version_.pos_r != 0` (i.e. only 3.3.5); `write_position()`
  only writes `pos_r` when both the client supports it *and*
  `pos.orientation.has_value()`. `teleport_step()` and
  `TeleportStepSession` apply the target's orientation once, on the
  final step only -- not on every intermediate step, so a smooth walk
  doesn't visibly spin the character until it snaps to face the
  requested direction at the very end.
- `services/favourites_porter.cpp`: the merge-dedup signature tuple now
  includes `orientation`, so missing/zero/non-zero orientations at the
  same coordinates are treated as distinct records (matches the
  description-included-in-signature rationale already in place).
- `ui/app.cpp`: favourites table gained a 5th "r" column (`-` placeholder
  when unset); the teleport log line includes `r=...` when present.
- `tests/orientation_tests.cpp` (new `orientation_tests` CTest target,
  wired into `.github/workflows/build.yml` via `ctest --test-dir build -C
  Release --output-on-failure` right after the Release build step, before
  artifact packaging): covers old/new/legacy-multiline favlist formats,
  the missing-vs-zero-vs-non-zero distinction end-to-end, save()
  round-trip normalisation, merge-dedup distinguishing by orientation, a
  `FakeBackend`-based test of 3.3.5's `pos_r` read/write-only-when-present,
  and confirms 1.12.1/1.12.3 never touch `pos_r` (guarded by
  `version_.pos_r == 0` for those profiles).
- The exact sample lines the user pasted (including the `#...#0` vs
  trailing-empty-`#` cases) were used verbatim as a test fixture and
  parse correctly.
- `README.md` and `使用说明.html` (§6 table reference, §7 file format)
  updated to document the new field/column.

**Verification actually performed this time** (see "Verification
methodology" above for why two techniques are used):
1. Real `g++ -std=c++17 -Wall -Wextra` compile+run of the pure-logic layer
   (`models.cpp`, `favlist_repository.cpp`, `favourites_service.cpp`,
   `favourites_porter.cpp`) against a throwaway harness using the user's
   exact sample data -- all assertions passed.
2. Regression-tested the *real* 1662-line `favlist.fav` shipped in this
   repo: loads unchanged (0 points with orientation, as expected for an
   all-old-format file), and round-trips stably through save/reload.
3. `tests/orientation_tests.cpp` itself: real g++ compile+link+run (using
   a throwaway `Sleep`/`QueryPerformanceCounter` shim so
   `teleport_service.cpp` would link on Linux) -- all assertions passed,
   including the `FakeBackend`-based 3.3.5 `pos_r` behaviour.
4. `clang++ -fsyntax-only` shim check for the Windows-facing files touched
   (`teleport_service.cpp`, `ui/app.cpp`) -- clean.
5. `cmake -S . -B <dir>` configure-only check on Linux (native compiler,
   no actual Win32 build) to validate `CMakeLists.txt` syntax after adding
   the `orientation_tests` target -- configured cleanly.
6. Pushed and polled the real `windows-latest` CI run to completion --
   see the commit log for the resulting run/commit; if this line hasn't
   been updated with a specific outcome, treat CI as **not yet confirmed
   green for this change** and check
   https://github.com/heshanu3d/TeleportHackDX/actions before trusting it.

**Confirmed green**: commit `29695e9`, CI run `30008276468` -- `Configure`,
`Build (Release)`, and `Test (Release)` (i.e. `ctest` running
`orientation_tests` for real on `windows-latest`, not just a Linux
approximation) all succeeded, artifact
`TeleportHackDX-0.1.0-g29695e9-win32` uploaded.

## Open user requests not yet addressed

- Nothing outstanding as of this writing. The user had separately asked to
  re-test window-bounds persistence in practice (only reasoned through +
  shim-checked + CI-green before, never hands-on confirmed) -- worth
  circling back to that if it comes up again.
