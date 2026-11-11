#include "ui/theme.h"
#include <algorithm>
#include "imgui.h"
#include "repository/settings_repository.h"

namespace th {
void apply_imgui_theme(const std::string& theme) {
    if (!ImGui::GetCurrentContext()) return;
    if (theme == "light") ImGui::StyleColorsLight();
    else ImGui::StyleColorsDark();
}
bool apply_interface_opacity(int opacity, HWND window, bool desktop) {
    const int bounded = std::clamp(opacity, kMinInterfaceOpacity, kMaxInterfaceOpacity);
    if (!desktop) {
        if (ImGui::GetCurrentContext()) ImGui::GetStyle().Alpha = static_cast<float>(bounded) / 100.0f;
        return true;
    }
    if (!window || !IsWindow(window)) return false;
    if (ImGui::GetCurrentContext()) ImGui::GetStyle().Alpha = 1.0f;
    const LONG_PTR old_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR new_style = bounded == 100 ? (old_style & ~static_cast<LONG_PTR>(WS_EX_LAYERED))
                                             : (old_style | WS_EX_LAYERED);
    if (new_style != old_style) {
        SetLastError(0);
        if (!SetWindowLongPtrW(window, GWL_EXSTYLE, new_style) && GetLastError() != 0) return false;
    }
    if (bounded < 100 && !SetLayeredWindowAttributes(window, 0, static_cast<BYTE>((bounded * 255 + 50) / 100), LWA_ALPHA)) {
        if (new_style != old_style) SetWindowLongPtrW(window, GWL_EXSTYLE, old_style);
        return false;
    }
    return true;
}
}
