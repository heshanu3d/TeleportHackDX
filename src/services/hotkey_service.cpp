#include "services/hotkey_service.h"

#include <cctype>

namespace th {

bool HotkeyService::to_win32(const HotkeyBinding& binding, UINT& modifiers, UINT& vk) const {
    modifiers = MOD_NOREPEAT;
    for (Modifier m : binding.modifiers()) {
        switch (m) {
            case Modifier::Shift: modifiers |= MOD_SHIFT; break;
            case Modifier::Ctrl: modifiers |= MOD_CONTROL; break;
            case Modifier::Alt: modifiers |= MOD_ALT; break;
        }
    }
    std::string key = binding.key();
    if (key.size() != 1) return false;
    unsigned char c = static_cast<unsigned char>(key[0]);
    if (std::isalpha(c)) {
        vk = static_cast<UINT>(std::toupper(c)); // VK codes for 'A'-'Z' == ASCII
        return true;
    }
    if (std::isdigit(c)) {
        vk = static_cast<UINT>(c); // VK codes for '0'-'9' == ASCII
        return true;
    }
    // Function keys typed as e.g. "F1" won't hit this path (key() returns
    // just the last character after stripping modifiers); not needed by
    // the original hotkey.txt format, which only uses single characters.
    return false;
}

int HotkeyService::apply(const std::vector<HotkeyBinding>& bindings,
                          const FavouritesService& favourites,
                          std::vector<std::string>& warnings) {
    clear();
    int bound = 0;
    for (const auto& binding : bindings) {
        const TeleportPoint* point = favourites.find_by_name(binding.point_name);
        if (!point) {
            warnings.push_back("hotkey " + binding.raw_combo + ": point '" + binding.point_name +
                                "' not found");
            continue;
        }
        UINT mods, vk;
        if (!to_win32(binding, mods, vk)) {
            warnings.push_back("hotkey " + binding.raw_combo + ": unrecognised key");
            continue;
        }
        int id = next_id_++;
        if (!RegisterHotKey(hwnd_, id, mods, vk)) {
            warnings.push_back("hotkey " + binding.raw_combo + ": RegisterHotKey failed (" +
                                std::to_string(GetLastError()) + ")");
            continue;
        }
        entries_.push_back(Entry{id, binding.raw_combo, binding.point_name, point->position});
        ++bound;
    }
    return bound;
}

void HotkeyService::clear() {
    for (const auto& e : entries_) UnregisterHotKey(hwnd_, e.id);
    entries_.clear();
}

bool HotkeyService::resolve(int id, Position& out) const {
    for (const auto& e : entries_) {
        if (e.id == id) {
            out = e.position;
            return true;
        }
    }
    return false;
}

} // namespace th
