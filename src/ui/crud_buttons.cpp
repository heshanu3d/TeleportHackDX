#include "ui/crud_buttons.h"

#include <algorithm>
#include "imgui.h"

namespace th {
const std::array<CrudButtonSpec, 9>& crud_button_specs() {
    static const std::array<CrudButtonSpec, 9> specs = {{
        {"AddPos", CrudAction::Add, 0},
        {"EditPost", CrudAction::Edit, 0},
        {"DelPos", CrudAction::Delete, 0},
        {"InsertPos", CrudAction::Insert, 1},
        {"AppendPos", CrudAction::Append, 1},
        {"Save", CrudAction::Save, 1},
        {"Reload", CrudAction::Reload, 1},
        {"\xe8\xaf\xbb\xe5\x8f\x96\xe5\x88\xb0\xe7\x82\x89\xe7\x9f\xb3", CrudAction::ReadToHearthstone, 2},
        {"\xe6\x8f\x92\xe5\x90\x8e \xe8\xa1\xa8&\xe7\x82\x89\xe7\x9f\xb3", CrudAction::AppendTableAndHearthstone, 2},
    }};
    return specs;
}

CrudAction render_crud_buttons(const Settings& settings, CrudButtonLayoutInfo* layout) {
    const auto& specs = crud_button_specs();
    const float spacing = static_cast<float>(std::clamp(settings.crud_button_spacing,
                                                         kMinCrudButtonSpacing, kMaxCrudButtonSpacing));
    const float requested_width = static_cast<float>(std::clamp(settings.crud_button_width,
                                                                kMinCrudButtonWidth, kMaxCrudButtonWidth));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    const float height = std::max(static_cast<float>(std::clamp(settings.crud_button_height,
                                         kMinCrudButtonHeight, kMaxCrudButtonHeight)),
                                  ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f);
    std::array<float, 9> widths{};
    float row_widths[3] = {};
    int row_counts[3] = {};
    for (size_t i = 0; i < specs.size(); ++i) {
        widths[i] = std::max(requested_width,
                             ImGui::CalcTextSize(specs[i].label).x + ImGui::GetStyle().FramePadding.x * 2.0f);
        const int row = specs[i].row;
        if (row_counts[row]++) row_widths[row] += spacing;
        row_widths[row] += widths[i];
    }
    const float content_width = std::max({row_widths[0], row_widths[1], row_widths[2]});
    const bool needs_scrollbar = content_width > ImGui::GetContentRegionAvail().x;
    if (layout) {
        *layout = {};
        layout->horizontal_overflow = needs_scrollbar;
    }
    const float panel_height = height * 3.0f + spacing * 2.0f +
                               (needs_scrollbar ? ImGui::GetStyle().ScrollbarSize : 0.0f);
    ImGui::SetNextWindowContentSize(ImVec2(content_width, 0.0f));
    CrudAction selected = CrudAction::None;
    if (ImGui::BeginChild("##coordinate_actions", ImVec2(0.0f, panel_height),
                          ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        int row = -1;
        for (size_t i = 0; i < specs.size(); ++i) {
            if (row == specs[i].row) ImGui::SameLine();
            row = specs[i].row;
            if (ImGui::Button(specs[i].label, ImVec2(widths[i], height))) selected = specs[i].action;
            if (layout) {
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                layout->rects[i] = {a.x, a.y, b.x, b.y};
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopFont();
    return selected;
}
}
