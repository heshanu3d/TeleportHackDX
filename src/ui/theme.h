#pragma once
#include <Windows.h>
#include <string>

namespace th {
// Changes only ImGui's palette; existing window and layout state is retained.
void apply_imgui_theme(const std::string& theme);
bool apply_interface_opacity(int opacity, HWND window, bool desktop);
}
