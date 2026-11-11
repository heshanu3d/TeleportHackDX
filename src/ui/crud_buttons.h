#pragma once
#include <array>
#include "repository/settings_repository.h"

namespace th {
enum class CrudAction {
    None, Add, Edit, Delete, Insert, Append, Save, Reload, ReadToHearthstone, AppendTableAndHearthstone
};
struct CrudButtonSpec {
    const char* label;
    CrudAction action;
    int row;
};
struct CrudButtonRect {
    float left = 0, top = 0, right = 0, bottom = 0;
};
struct CrudButtonLayoutInfo {
    std::array<CrudButtonRect, 9> rects{};
    bool horizontal_overflow = false;
};
const std::array<CrudButtonSpec, 9>& crud_button_specs();
// Optional geometry supports layout and click regression tests.
CrudAction render_crud_buttons(const Settings& settings, CrudButtonLayoutInfo* layout = nullptr);
}
