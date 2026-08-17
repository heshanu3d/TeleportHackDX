// TeleportHackDX C ABI.
//
// Exposes just enough of th_core for external processes (WPF/WinForms C#
// UIs, or any other language with a C FFI) to attach to a running WoW.exe
// via ReadProcessMemory/WriteProcessMemory and drive the same underlying
// TeleportService/FeatureService/FavouritesService the ImGui builds use.
//
// Design constraints:
//   * Every function is `extern "C"` and uses only fundamental C types /
//     C-strings, so P/Invoke on any target framework can bind directly.
//   * Strings are UTF-8 in both directions. Buffer-out functions take an
//     int32_t byte capacity and return the number of bytes needed
//     (excluding NUL), so callers can right-size.
//   * The typical return convention is `0` on success and `-1` on failure.
//     Failure details live in a per-session UTF-8 error string retrievable
//     via th_session_last_error().
//   * The API is intentionally minimal (MVP: attach, version, position,
//     teleport, speed hacks, four feature toggles, favourites read-only).
//     Additional functionality is added by appending new functions -- never
//     by changing an existing signature -- so we don't break already
//     shipped C# clients.
//
// Threading: not internally synchronised. All calls on a given th_session
// must be serialised by the caller (e.g. a single UI thread + timer that
// posts to the same thread).
//
// This DLL is orthogonal to TeleportHackDX.dll: it is *not* injected into
// the game, only loaded by external tools. The injection DLL keeps its
// existing static th_core link and does not depend on th_capi.
#pragma once

#include <stdint.h>

#if defined(_WIN32)
#  if defined(TH_CAPI_BUILDING)
#    define TH_CAPI_EXPORT __declspec(dllexport)
#  else
#    define TH_CAPI_EXPORT __declspec(dllimport)
#  endif
#  define TH_CAPI_CALL __cdecl
#else
#  define TH_CAPI_EXPORT
#  define TH_CAPI_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------------------
// Opaque handle. Allocate with th_session_create, release with
// th_session_destroy. NULL indicates the operation failed to allocate.
// -------------------------------------------------------------------------
typedef struct th_session th_session;

// -------------------------------------------------------------------------
// Versions
// -------------------------------------------------------------------------

// Returns the number of known WoW client profiles. Always > 0.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_version_count(void);

// Copies the profile name (UTF-8) at `index` into `out`, up to `out_size`
// bytes including the terminating NUL. Returns the number of bytes needed
// for the full string (excluding NUL); if that value is >= `out_size` the
// output was truncated. Returns -1 if `index` is out of range or `out` is
// NULL with a positive `out_size`.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_version_name(int32_t index, char* out,
                                                      int32_t out_size);

// -------------------------------------------------------------------------
// Process listing (no session required)
// -------------------------------------------------------------------------

typedef struct {
    uint32_t pid;
    char exe_name[64]; // UTF-8, NUL-terminated
} th_process_entry;

// Enumerates running processes whose executable matches `executable` (case
// insensitive, e.g. "WoW.exe"). Writes up to `max_entries` results into
// `out`. Returns the number of entries written, or -1 on Toolhelp failure.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_process_list(const char* executable,
                                                     th_process_entry* out,
                                                     int32_t max_entries);

// -------------------------------------------------------------------------
// Session lifecycle
// -------------------------------------------------------------------------

// Creates a new session with the default (3.3.5) version and no attached
// process. Returns NULL on allocation failure.
TH_CAPI_EXPORT th_session* TH_CAPI_CALL th_session_create(void);

// Destroys a session and detaches from any attached process. Safe to call
// with NULL.
TH_CAPI_EXPORT void TH_CAPI_CALL th_session_destroy(th_session* session);

// Copies the last error message (UTF-8) into `out`. Same size semantics as
// th_version_name. Returns 0 if there is no error currently set.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_last_error(th_session* session,
                                                            char* out,
                                                            int32_t out_size);

// -------------------------------------------------------------------------
// Attach / detach
// -------------------------------------------------------------------------

TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_attach(th_session* session, uint32_t pid);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_detach(th_session* session);

// Returns 1 if attached, 0 if not, -1 if the session is invalid.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_is_attached(th_session* session);
TH_CAPI_EXPORT uint32_t TH_CAPI_CALL th_session_pid(th_session* session);

// -------------------------------------------------------------------------
// Version selection
// -------------------------------------------------------------------------

TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_set_version(th_session* session,
                                                             const char* name);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_get_version(th_session* session,
                                                             char* out,
                                                             int32_t out_size);

// -------------------------------------------------------------------------
// Position / teleport
// -------------------------------------------------------------------------

// Fills `x`, `y`, `z` with the current player position. On 3.3.5 also
// fills `r` (orientation in radians) and sets `has_r` to 1; on 1.12.x
// `has_r` is set to 0 and `r` is left unchanged. Any out-pointer may be
// NULL to skip. Returns 0 on success, -1 on failure.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_read_position(th_session* session,
                                                               double* x, double* y,
                                                               double* z, double* r,
                                                               int32_t* has_r);

// Writes the target position. Set `has_r` to non-zero to also write
// orientation on clients that support it (silently ignored on 1.12.x).
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_teleport(th_session* session,
                                                          double x, double y, double z,
                                                          int32_t has_r, double r);

// -------------------------------------------------------------------------
// Speed hacks
// -------------------------------------------------------------------------

TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_set_global_speed(th_session* session,
                                                                  double value);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_set_movement_speed(th_session* session,
                                                                    double value);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_reset_movement_speed(th_session* session);

// -------------------------------------------------------------------------
// Feature toggles. `*now_on` is set to 1 or 0 on success.
// -------------------------------------------------------------------------

TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_toggle_anti_jump(th_session* session,
                                                                  int32_t* now_on);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_toggle_autoloot(th_session* session,
                                                                 int32_t* now_on);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_toggle_patch_loot(th_session* session,
                                                                   int32_t* now_on);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_toggle_lua_unlock(th_session* session,
                                                                    int32_t* now_on);

// -------------------------------------------------------------------------
// Favourites (read-only for MVP)
// -------------------------------------------------------------------------

// Loads points from a favlist.fav path (UTF-8). Existing loaded points are
// replaced. Returns 0 on success, -1 on failure.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_load(th_session* session,
                                                         const char* path);

// Number of loaded points.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_count(th_session* session);

// Description / category buffers use th_version_name's size semantics.
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_get_description(th_session* session,
                                                                    int32_t index,
                                                                    char* out,
                                                                    int32_t out_size);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_get_category(th_session* session,
                                                                 int32_t index, char* out,
                                                                 int32_t out_size);
TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_get_position(th_session* session,
                                                                 int32_t index, double* x,
                                                                 double* y, double* z,
                                                                 double* r, int32_t* has_r);

#ifdef __cplusplus
} // extern "C"
#endif
