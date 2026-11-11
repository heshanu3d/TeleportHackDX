#pragma once

#include <Windows.h>
#include <atomic>

namespace th {
enum CollapseModifier : unsigned { CollapseCtrl = 1, CollapseShift = 2, CollapseAlt = 4, CollapseWin = 8 };

class CollapseShortcut {
public:
    void configure(int key, unsigned modifiers);
    bool handle(UINT message, WPARAM key, LPARAM lparam, unsigned modifiers,
                bool visible, bool foreground, bool text_input);
    // Called within the main ImGui window's Begin/End scope.
    void apply_pending();
private:
    std::atomic<unsigned> config_{0};
    std::atomic<unsigned> pending_{0};
    std::atomic<unsigned> consumed_key_{0};
    std::atomic<unsigned> consumed_scan_{0};
};
}
