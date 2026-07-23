// Wire hotkey.txt bindings to Win32 global hotkeys.
//
// Unlike the Python port (which used a pynput listener thread + Qt signal
// marshalling), we're running inside the game process, so plain
// RegisterHotKey/WM_HOTKEY on the game's own window is both simpler and
// works even while the game (not our ImGui overlay) has focus -- matching
// the original AutoIt HotKeySet() behaviour.
//
// Conceptually ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/hotkey_service.py
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "domain/models.h"
#include "services/favourites_service.h"

namespace th {

class HotkeyService {
public:
    explicit HotkeyService(HWND hwnd) : hwnd_(hwnd) {}
    ~HotkeyService() { clear(); }

    HotkeyService(const HotkeyService&) = delete;
    HotkeyService& operator=(const HotkeyService&) = delete;

    // Unregisters everything currently bound, then registers `bindings`
    // that resolve to a known favourite. Returns the number actually
    // bound; unresolved bindings are skipped (reported via `warnings`).
    int apply(const std::vector<HotkeyBinding>& bindings, const FavouritesService& favourites,
               std::vector<std::string>& warnings);

    void clear();

    // Called from the hooked WndProc on WM_HOTKEY. Returns true and fills
    // `out` if `id` is one of ours.
    bool resolve(int id, Position& out) const;

private:
    struct Entry {
        int id = 0;
        std::string raw_combo;
        std::string point_name;
        Position position;
    };

    HWND hwnd_;
    std::vector<Entry> entries_;
    int next_id_ = 1; // WM_HOTKEY ids must be > 0 (0 reserved by owner-draw menus etc.)

    // Returns false if the combo's trailing key isn't a single recognised
    // character.
    bool to_win32(const HotkeyBinding& binding, UINT& modifiers, UINT& vk) const;
};

} // namespace th
