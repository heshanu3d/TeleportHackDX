#include "ui/favourites_table.h"

#include "imgui.h"

namespace th {
void setup_favourite_columns(const std::array<bool, 5>& show_columns) {
    ImGui::TableSetupColumn("\xe6\x8f\x8f\xe8\xbf\xb0", ImGuiTableColumnFlags_WidthStretch);
    const char* labels[] = {"x", "y", "z", "r", "map"};
    const float widths[] = {60.0f, 60.0f, 60.0f, 44.0f, 36.0f};
    for (size_t i = 0; i < show_columns.size(); ++i) {
        const ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthFixed |
            (show_columns[i] ? ImGuiTableColumnFlags_None : ImGuiTableColumnFlags_Disabled);
        ImGui::TableSetupColumn(labels[i], flags, widths[i]);
    }
}
}
