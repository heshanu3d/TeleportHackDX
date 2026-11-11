#include "ui/collapse_shortcut.h"

#include "imgui.h"
#include "repository/settings_repository.h"

namespace th {
void CollapseShortcut::configure(int key, unsigned modifiers) {
    config_.store(static_cast<unsigned>(sanitize_collapse_hotkey(key)) | ((modifiers & 7u) << 8));
}

bool CollapseShortcut::handle(UINT message, WPARAM key, LPARAM lparam, unsigned modifiers,
                              bool visible, bool foreground, bool text_input) {
    const auto scan = static_cast<unsigned>((static_cast<unsigned long long>(lparam) >> 16) & 0x1ffu);
    if (message == WM_CHAR || message == WM_SYSCHAR || message == WM_DEADCHAR || message == WM_SYSDEADCHAR)
        return scan != 0 && scan == consumed_scan_.load();
    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        unsigned consumed = consumed_key_.load();
        if (consumed && consumed == key && consumed_key_.compare_exchange_strong(consumed, 0)) return true;
        return false;
    }
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
    const bool repeat = (static_cast<unsigned long long>(lparam) & (1ull << 30)) != 0;
    if (repeat && consumed_key_.load() == key) return true;
    consumed_scan_.store(0);
    const unsigned config = config_.load();
    if (!visible || !foreground || text_input || !(config & 0xffu) || key != (config & 0xffu) ||
        modifiers != (config >> 8)) return false;
    consumed_key_.store(static_cast<unsigned>(key));
    consumed_scan_.store(scan);
    if (!repeat) pending_.fetch_add(1);
    return true;
}

void CollapseShortcut::apply_pending() {
    if (pending_.exchange(0) & 1u) ImGui::SetWindowCollapsed(!ImGui::IsWindowCollapsed());
}
}
