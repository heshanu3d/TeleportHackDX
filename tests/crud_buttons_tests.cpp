#include "ui/crud_buttons.h"
#include "ui/collapse_shortcut.h"
#include "ui/favourites_table.h"

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool nearly_equal(float a, float b) { return std::fabs(a - b) < 1.1f; }

bool collapse_frame(th::CollapseShortcut& shortcut, bool force_collapsed = false) {
    ImGui::NewFrame();
    ImGui::Begin("collapse-test");
    if (force_collapsed) ImGui::SetWindowCollapsed(true);
    shortcut.apply_pending();
    const bool collapsed = ImGui::IsWindowCollapsed();
    ImGui::End();
    ImGui::Render();
    return collapsed;
}

void check_collapse_shortcut() {
    th::CollapseShortcut shortcut;
    check(!shortcut.handle(WM_KEYDOWN, VK_F8, 0, 0, true, true, false), "shortcut starts disabled");
    shortcut.configure(VK_F8, th::CollapseCtrl | th::CollapseShift);
    check(!shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl, true, true, false), "requires exact modifiers");
    check(!shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift | th::CollapseWin, true, true, false), "rejects Windows key modifier");
    check(!shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift, false, true, false), "hidden overlay ignores shortcut");
    check(!shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift, true, false, false), "background game ignores shortcut");
    check(!shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift, true, true, true), "text input ignores shortcut");
    check(!collapse_frame(shortcut), "window initially expanded");
    check(shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift, true, true, false), "matching shortcut is consumed");
    check(shortcut.handle(WM_KEYDOWN, VK_F8, 1L << 30, 0, true, true, false), "held key repeats are consumed");
    check(shortcut.handle(WM_KEYUP, VK_F8, 0, 0, false, false, false), "matching key release is consumed even when hidden");
    check(!collapse_frame(shortcut), "collapse is queued for the next frame");
    check(collapse_frame(shortcut), "shortcut collapses window once despite repeat");
    check(shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift, true, true, false), "shortcut can be pressed again");
    check(collapse_frame(shortcut), "expand is queued for the next frame");
    check(!collapse_frame(shortcut), "shortcut expands collapsed window");
    shortcut.handle(WM_KEYUP, VK_F8, 0, 0, true, true, false);
    collapse_frame(shortcut, true);
    check(collapse_frame(shortcut), "mouse-style collapse is reflected in the actual window state");
    check(shortcut.handle(WM_KEYDOWN, VK_F8, 0, th::CollapseCtrl | th::CollapseShift, true, true, false), "shortcut remains available after mouse toggle");
    collapse_frame(shortcut);
    check(!collapse_frame(shortcut), "shortcut uses actual window collapse state");
    shortcut.handle(WM_KEYUP, VK_F8, 0, 0, true, true, false);
    shortcut.configure('A', 0);
    const LPARAM scan = 0x1eL << 16;
    check(shortcut.handle(WM_KEYDOWN, 'A', scan, 0, true, true, false), "letter shortcut is consumed");
    check(shortcut.handle(WM_CHAR, 'a', scan, 0, true, true, false), "translated shortcut character is consumed");
    check(!shortcut.handle(WM_KEYDOWN, 'B', 0x30L << 16, 0, true, true, false) &&
              !shortcut.handle(WM_CHAR, 'b', 0x30L << 16, 0, true, true, false),
          "unrelated key and character still pass through");
}

void check_table_columns(const std::array<bool, 5>& show_columns) {
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_Always);
    ImGui::Begin("table-test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::BeginTable("favourites", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          ImVec2(0, 250))) {
        th::setup_favourite_columns(show_columns);
        ImGui::TableHeadersRow();
        check((ImGui::TableGetColumnFlags(0) & ImGuiTableColumnFlags_IsEnabled) != 0,
              "description column remains visible");
        for (int i = 1; i < 6; ++i)
            check(((ImGui::TableGetColumnFlags(i) & ImGuiTableColumnFlags_IsEnabled) != 0) == show_columns[i - 1],
                  "coordinate column visibility matches checkbox " + std::to_string(i));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Selectable("test coordinate", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
        ImGui::EndTable();
    } else {
        check(false, "favourites table opens");
    }
    ImGui::End();
    ImGui::Render();
}

struct Frame {
    th::CrudAction action = th::CrudAction::None;
    th::CrudButtonLayoutInfo layout;
    std::array<float, 9> text_widths{};
    float font_before = 0, font_after = 0, enlarged_font = 0;
    ImVec2 spacing_before{}, spacing_after{};
    bool scrollbar = false;
    int vertices = 0;
};

Frame render_frame(const th::Settings& settings, float window_width) {
    Frame result;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(window_width, 440), ImGuiCond_Always);
    ImGui::Begin("layout-test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings);
    result.font_before = ImGui::GetFontSize();
    result.spacing_before = ImGui::GetStyle().ItemSpacing;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    result.enlarged_font = ImGui::GetFontSize();
    const auto& specs = th::crud_button_specs();
    for (size_t i = 0; i < specs.size(); ++i) result.text_widths[i] = ImGui::CalcTextSize(specs[i].label).x;
    ImGui::PopFont();
    result.action = th::render_crud_buttons(settings, &result.layout);
    result.font_after = ImGui::GetFontSize();
    result.spacing_after = ImGui::GetStyle().ItemSpacing;
    ImGui::End();
    ImGui::Render();
    result.vertices = ImGui::GetDrawData()->TotalVtxCount;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        if (std::strstr(window->Name, "##coordinate_actions")) result.scrollbar = window->ScrollbarX;
    }
    return result;
}
}

int main() {
    const auto& specs = th::crud_button_specs();
    const char* labels[] = {"AddPos", "EditPost", "DelPos", "InsertPos", "AppendPos", "Save", "Reload",
        "\xe8\xaf\xbb\xe5\x8f\x96\xe5\x88\xb0\xe7\x82\x89\xe7\x9f\xb3",
        "\xe6\x8f\x92\xe5\x90\x8e \xe8\xa1\xa8&\xe7\x82\x89\xe7\x9f\xb3"};
    const th::CrudAction actions[] = {th::CrudAction::Add, th::CrudAction::Edit, th::CrudAction::Delete,
        th::CrudAction::Insert, th::CrudAction::Append, th::CrudAction::Save, th::CrudAction::Reload,
        th::CrudAction::ReadToHearthstone, th::CrudAction::AppendTableAndHearthstone};
    const int rows[] = {0, 0, 0, 1, 1, 1, 1, 2, 2};
    for (size_t i = 0; i < specs.size(); ++i) {
        check(std::string(specs[i].label) == labels[i] && specs[i].action == actions[i] &&
                  specs[i].row == rows[i], "label, action and row " + std::to_string(i));
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1024, 768);
    io.DeltaTime = 1.0f / 60.0f;
    char windows[MAX_PATH] = {};
    GetWindowsDirectoryA(windows, MAX_PATH);
    const std::string font_path = std::string(windows) + "\\Fonts\\msyh.ttc";
    if (GetFileAttributesA(font_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 18.0f);
    else
        io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    check_collapse_shortcut();

    for (unsigned mask = 0; mask < 32; ++mask) {
        std::array<bool, 5> columns{};
        for (unsigned i = 0; i < columns.size(); ++i) columns[i] = (mask & (1u << i)) != 0;
        check_table_columns(columns);
    }
    check_table_columns({true, true, true, true, true});

    th::Settings settings;
    Frame frame = render_frame(settings, 650);
    frame = render_frame(settings, 650);
    check(frame.vertices > 0, "button panel emits visible draw geometry");
    check(frame.enlarged_font > frame.font_before && nearly_equal(frame.font_before, frame.font_after),
          "button font is enlarged and restored after rendering");
    check(nearly_equal(frame.spacing_before.x, frame.spacing_after.x) && nearly_equal(frame.spacing_before.y, frame.spacing_after.y),
          "panel spacing does not leak into other controls");
    check(!frame.layout.horizontal_overflow, "wide window fits the default rows");
    Frame default_window = render_frame(settings, 480);
    default_window = render_frame(settings, 480);
    check(!default_window.layout.horizontal_overflow, "default 480-pixel window fits the default rows");
    frame = render_frame(settings, 650);
    const auto& rect = frame.layout.rects;
    for (size_t i = 0; i < rect.size(); ++i) {
        check(rect[i].right - rect[i].left >= frame.text_widths[i] + 1.0f,
              "label fits inside button " + std::to_string(i));
        check(rect[i].bottom - rect[i].top >= settings.crud_button_height - 1.0f,
              "configured height applies to button " + std::to_string(i));
        if (i > 0 && rows[i] == rows[i - 1]) {
            check(nearly_equal(rect[i].top, rect[i - 1].top) &&
                      rect[i].left >= rect[i - 1].right + settings.crud_button_spacing - 1.0f,
                  "buttons do not overlap in row " + std::to_string(rows[i]));
        }
        if (i > 0 && rows[i] != rows[i - 1])
            check(rect[i].top >= rect[i - 1].bottom + settings.crud_button_spacing - 1.0f,
                  "successive rows respect spacing");
    }
    for (size_t i = 0; i < rect.size(); ++i) {
        const auto target = frame.layout.rects[i];
        io.AddMousePosEvent((target.left + target.right) * 0.5f, (target.top + target.bottom) * 0.5f);
        render_frame(settings, 650);
        io.AddMouseButtonEvent(0, true);
        render_frame(settings, 650);
        io.AddMouseButtonEvent(0, false);
        frame = render_frame(settings, 650);
        check(frame.action == actions[i], "click dispatch for button " + std::to_string(i));
    }
    settings.crud_button_width = th::kMaxCrudButtonWidth;
    settings.crud_button_height = th::kMaxCrudButtonHeight;
    settings.crud_button_spacing = th::kMaxCrudButtonSpacing;
    frame = render_frame(settings, 300);
    frame = render_frame(settings, 300);
    check(frame.layout.horizontal_overflow && frame.scrollbar,
          "narrow window exposes horizontal scrolling for wide buttons");
    check(frame.layout.rects[0].bottom - frame.layout.rects[0].top >= 79.0f,
          "configured larger button height is applied");
    check(frame.layout.rects[0].right - frame.layout.rects[0].left >= 239.0f &&
              frame.layout.rects[1].left - frame.layout.rects[0].right >= 31.0f,
          "configured larger button width and spacing are applied");
    ImGuiWindow* child = nullptr;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        if (std::strstr(window->Name, "##coordinate_actions")) child = window;
    }
    check(child != nullptr && child->ScrollMax.x > 0.0f, "scrollable child has a horizontal scroll range");
    if (child && child->ScrollMax.x > 0.0f) {
        ImGui::SetScrollX(child, child->ScrollMax.x);
        frame = render_frame(settings, 300);
        frame = render_frame(settings, 300);
        const auto reload = frame.layout.rects[6];
        check(child->Scroll.x > 0.0f && reload.left < child->InnerClipRect.Max.x &&
                  reload.right > child->InnerClipRect.Min.x,
              "scrolling exposes the rightmost second-row button");
        io.AddMousePosEvent((reload.left + reload.right) * 0.5f, (reload.top + reload.bottom) * 0.5f);
        render_frame(settings, 300);
        io.AddMouseButtonEvent(0, true);
        render_frame(settings, 300);
        io.AddMouseButtonEvent(0, false);
        frame = render_frame(settings, 300);
        check(frame.action == th::CrudAction::Reload, "rightmost button works after horizontal scrolling");
    }
    settings.crud_button_width = th::kMinCrudButtonWidth;
    settings.crud_button_height = th::kMinCrudButtonHeight;
    settings.crud_button_spacing = th::kMinCrudButtonSpacing;
    frame = render_frame(settings, 480);
    frame = render_frame(settings, 480);
    for (size_t i = 0; i < frame.layout.rects.size(); ++i) {
        const auto current = frame.layout.rects[i];
        check(current.right - current.left >= frame.text_widths[i] + 1.0f,
              "minimum width still fits label " + std::to_string(i));
        if (i > 0 && rows[i] == rows[i - 1])
            check(current.left >= frame.layout.rects[i - 1].right - 1.0f,
                  "zero spacing does not overlap buttons");
    }
    ImGui::DestroyContext();
    if (failures) return 1;
    std::cout << "CRUD button layout and click tests passed\n";
}
