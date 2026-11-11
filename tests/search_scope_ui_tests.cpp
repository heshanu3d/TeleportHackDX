#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "domain/versions.h"
#include "repository/favlist_repository.h"
#include "repository/settings_repository.h"
#include "ui/app.h"

namespace {
const std::string dungeon = "\xe5\x89\xaf\xe6\x9c\xac" "60";
const std::string profession = "\xe4\xb8\x93\xe4\xb8\x9a";
const std::string scarlet = "\xe8\xa1\x80\xe8\x89\xb2";
int failures = 0;
class FakeMemory final : public th::MemoryBackend {
public:
    uint32_t current_map = 0;
    uintptr_t map_address = 0;
    std::unordered_map<uintptr_t, float> floats;
    std::unordered_map<uintptr_t, uintptr_t> pointers;
    int writes = 0;
    bool is_attached() const override { return true; }
    bool read_float(uintptr_t address, float& out) override {
        const auto it = floats.find(address);
        if (it == floats.end()) return false;
        out = it->second; return true;
    }
    bool read_uint32(uintptr_t address, uint32_t& out) override {
        if (address != map_address) return false;
        out = current_map; return true;
    }
    bool read_byte(uintptr_t, uint8_t&) override { return false; }
    bool read_pointer(uintptr_t address, uintptr_t& out) override {
        const auto it = pointers.find(address);
        if (it == pointers.end()) return false;
        out = it->second; return true;
    }
    bool read_bytes(uintptr_t, void*, size_t) override { return false; }
    bool write_float(uintptr_t address, float value) override {
        floats[address] = value; ++writes; return true;
    }
    bool write_byte(uintptr_t, uint8_t) override { return false; }
    void prepare(const th::GameVersion& version) {
        map_address = version.map_id;
        floats[version.curr_pos_x] = 1;
        floats[version.curr_pos_y] = 2;
        floats[version.curr_pos_z] = 3;
        uintptr_t next = 0x10000;
        const auto chain = [&](const std::vector<uintptr_t>& offsets) {
            uintptr_t address = version.static_player;
            for (uintptr_t offset : offsets) {
                auto it = pointers.find(address);
                if (it == pointers.end()) {
                    it = pointers.emplace(address, next).first;
                    next += 0x10000;
                }
                address = it->second + offset;
            }
            return address;
        };
        chain(version.dst_x_offsets);
        chain(version.dst_y_offsets);
        chain(version.dst_z_offsets);
    }
};
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void frame(th::App& app) { ImGui::NewFrame(); app.render(); ImGui::Render(); }
void create_imgui(bool chinese_font = false) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1200, 1000);
    io.DeltaTime = 1.0f / 60.0f;
    if (chinese_font) {
        char windows[MAX_PATH]{};
        GetWindowsDirectoryA(windows, MAX_PATH);
        const std::string font = std::string(windows) + "\\Fonts\\msyh.ttc";
        if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig config;
            config.OversampleH = config.OversampleV = 1;
            io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f, &config,
                                         io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
}
struct Ids {
    ImGuiID points, navigation, input, independent, api, right_click, category_settings;
};
Ids ids() {
    ImGuiWindow* window = ImGui::FindWindowByName("TeleportHack DX");
    const ImGuiID bar = window->GetID("##main_tabs");
    const ImGuiID points = ImHashStr("\xe5\x9d\x90\xe6\xa0\x87\xe7\x82\xb9", 0, bar);
    return {points, ImHashStr("\xe5\x88\x86\xe7\xb1\xbb\xe5\xaf\xbc\xe8\x88\xaa", 0, bar),
            ImHashStr("##search", 0, points),
            ImHashStr("\xe7\x8b\xac\xe7\xab\x8b\xe6\x90\x9c\xe7\xb4\xa2", 0, points),
            ImHashStr("\xe4\xbc\xa0\xe9\x80\x81" "api", 0, points),
            ImHashStr("\xe5\x8f\xb3\xe9\x94\xae\xe4\xbc\xa0\xe9\x80\x81", 0, points),
            ImHashStr("\xe5\x88\x86\xe7\xb1\xbb\xe8\xae\xbe\xe7\xbd\xae", 0, bar)};
}
void activate(th::App& app, ImGuiID id) { ImGui::ActivateItemByID(id); frame(app); }
std::string focus_search(th::App& app, ImGuiID id) {
    activate(app, id);
    const ImGuiContext* ctx = ImGui::GetCurrentContext();
    check(ctx->ActiveId == id && ctx->InputTextState.TextA.Data, "search input receives focus");
    return ctx->ActiveId == id && ctx->InputTextState.TextA.Data ? ctx->InputTextState.TextA.Data : "<unfocused>";
}
void choose_category(th::App& app, const Ids& id, const std::string& category, int group = 0) {
    activate(app, id.navigation);
    frame(app);
    const ImGuiID child_id = ImHashStr("##navigation", 0, id.navigation);
    ImGuiWindow* child = nullptr;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        if (window->ChildId == child_id) child = window;
    check(child != nullptr, "category navigation is available");
    if (!child) return;
    const ImGuiID scope = ImHashData(&group, sizeof(group), child->ID);
    const ImGuiID table = ImHashStr("##grid", 0, scope);
    activate(app, ImHashStr(category.c_str(), 0, table));
    frame(app);
    frame(app);
}
void select_combo_entry(th::App& app, ImGuiID combo, int index, const std::string& name) {
    activate(app, combo);
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context->OpenPopupStack.Size == 0 || !context->OpenPopupStack.back().Window) {
        check(false, "category settings combo opens");
        return;
    }
    const ImGuiID scope = ImHashData(&index, sizeof(index), context->OpenPopupStack.back().Window->ID);
    activate(app, ImHashStr(name.c_str(), 0, scope));
}

std::string file_bytes(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void test_category_settings(th::SettingsRepository& repo) {
    std::string error;
    th::FavlistRepository favourites("favlist.fav");
    check(favourites.save({{dungeon + "-entry", {1, 2, 3}},
                           {profession + "-entry", {4, 5, 6}}}, error) == 2,
          "category settings fixture saves");
    const std::string original_favourites = file_bytes("favlist.fav");
    th::Settings settings;
    check(repo.save(settings, error), "category settings initial state saves");
    create_imgui(true);
    {
        th::App app;
        app.initialize(nullptr, th::BackendMode::InProcess);
        frame(app); frame(app);
        const Ids id = ids();
        activate(app, id.category_settings); frame(app);
        const ImGuiID name_input = ImHashStr("##category_group_name", 0, id.category_settings);
        activate(app, name_input);
        ImGui::GetIO().AddInputCharactersUTF8("Custom"); frame(app);
        const ImGuiID add = ImHashStr("\xe6\x96\xb0\xe5\xa2\x9e##category_group", 0, id.category_settings);
        activate(app, add);
        auto saved = repo.load();
        check(saved.category_groups_customized && saved.customized_category_groups.size() == 6 &&
                  saved.customized_category_groups[5].name == "Custom", "category settings page adds a group");
        activate(app, name_input);
        ImGui::GetIO().AddInputCharactersUTF8("Instances"); frame(app);
        const ImGuiID rename = ImHashStr("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d##category_group", 0, id.category_settings);
        activate(app, rename);
        check(repo.load().customized_category_groups[5].name == "Instances", "category settings page renames a group");
        const ImGuiID up = ImHashStr("\xe5\xa4\xa7\xe7\xb1\xbb\xe4\xb8\x8a\xe7\xa7\xbb##category_group", 0, id.category_settings);
        const ImGuiID down = ImHashStr("\xe5\xa4\xa7\xe7\xb1\xbb\xe4\xb8\x8b\xe7\xa7\xbb##category_group", 0, id.category_settings);
        activate(app, up);
        check(repo.load().customized_category_groups[4].name == "Instances", "category settings page moves a group up");
        activate(app, down);
        check(repo.load().customized_category_groups[5].name == "Instances", "category settings page moves a group down");
        const ImGuiID selector = ImHashStr("##category_group_select", 0, id.category_settings);
        select_combo_entry(app, selector, 0, "\xe6\x9d\x82\xe9\xa1\xb9");
        const ImGuiID child_id = ImHashStr("##category_group_items", 0, id.category_settings);
        ImGuiWindow* child = nullptr;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            if (window->ChildId == child_id) child = window;
        check(child != nullptr, "category settings list is visible");
        if (child) {
            const int first = 0;
            const ImGuiID item_scope = ImHashData(&first, sizeof(first), child->ID);
            activate(app, ImHashStr(dungeon.c_str(), 0, item_scope));
            const ImGuiID target = ImHashStr("\xe7\xa7\xbb\xe5\x8a\xa8\xe8\x87\xb3##category_group_target", 0, id.category_settings);
            select_combo_entry(app, target, 5, "Instances");
            const ImGuiID move = ImHashStr("\xe7\xa7\xbb\xe5\x8a\xa8\xe5\xb0\x8f\xe7\xb1\xbb##category_group", 0, id.category_settings);
            activate(app, move);
            saved = repo.load();
            check(saved.customized_category_groups.size() == 6 &&
                      saved.customized_category_groups[5].categories == std::vector<std::string>{dungeon},
                  "category settings page moves a small category to the chosen group");
            select_combo_entry(app, selector, 0, "\xe6\x9d\x82\xe9\xa1\xb9");
            activate(app, ImHashStr(profession.c_str(), 0, item_scope));
            select_combo_entry(app, target, 5, "Instances");
            activate(app, move);
            const ImGuiID item_up = ImHashStr("\xe5\xb0\x8f\xe7\xb1\xbb\xe4\xb8\x8a\xe7\xa7\xbb##category_group", 0, id.category_settings);
            const ImGuiID item_down = ImHashStr("\xe5\xb0\x8f\xe7\xb1\xbb\xe4\xb8\x8b\xe7\xa7\xbb##category_group", 0, id.category_settings);
            activate(app, item_up);
            check(repo.load().customized_category_groups[5].categories == std::vector<std::string>{profession, dungeon},
                  "category settings page moves a small category up");
            activate(app, item_down);
            check(repo.load().customized_category_groups[5].categories == std::vector<std::string>{dungeon, profession},
                  "category settings page moves a small category down");
            choose_category(app, id, dungeon, 5);
            check(repo.load().last_category == dungeon, "navigation selects a category in its new group");
        }
        check(file_bytes("favlist.fav") == original_favourites, "group management leaves favourite bytes unchanged");
        app.shutdown();
    }
    ImGui::DestroyContext();
    create_imgui(true);
    {
        th::App app;
        app.initialize(nullptr, th::BackendMode::InProcess);
        frame(app); frame(app);
        const Ids id = ids();
        auto saved = repo.load();
        check(saved.category_groups_customized && saved.customized_category_groups[5].name == "Instances" &&
                  saved.customized_category_groups[5].categories == std::vector<std::string>{dungeon, profession},
              "category group and membership survive restart");
        choose_category(app, id, dungeon, 5);
        check(repo.load().last_category == dungeon, "navigation restores the saved custom group after restart");
        activate(app, id.category_settings); frame(app);
        const ImGuiID selector = ImHashStr("##category_group_select", 0, id.category_settings);
        select_combo_entry(app, selector, 5, "Instances");
        const ImGuiID remove = ImHashStr("\xe5\x88\xa0\xe9\x99\xa4##category_group", 0, id.category_settings);
        activate(app, remove);
        saved = repo.load();
        check(saved.customized_category_groups.size() == 5 &&
                  std::find(saved.customized_category_groups[0].categories.begin(),
                            saved.customized_category_groups[0].categories.end(), dungeon) !=
                      saved.customized_category_groups[0].categories.end(),
              "deleting a group moves its categories into the first group");
        check(file_bytes("favlist.fav") == original_favourites, "deleting a group leaves favourite bytes unchanged");
        app.shutdown();
    }
    ImGui::DestroyContext();
}
void test_category_multiselect(th::SettingsRepository& repo) {
    std::string error;
    th::FavlistRepository favourites("favlist.fav");
    check(favourites.save({{"A-entry", {1,2,3}}, {"B-entry", {1,2,3}},
                           {"C-entry", {1,2,3}}, {"D-entry", {1,2,3}},
                           {"E-entry", {1,2,3}}, {"X-entry", {1,2,3}}}, error) == 6,
          "multiselect fixture saves");
    th::Settings settings;
    settings.category_groups_customized = true;
    settings.customized_category_groups = {{"Source", {"A", "B", "C", "D", "E"}}, {"Target", {"X"}}};
    check(repo.save(settings, error), "multiselect settings save");
    create_imgui(true);
    {
        th::App app;
        app.initialize(nullptr, th::BackendMode::InProcess);
        frame(app); frame(app);
        const Ids id = ids();
        activate(app, id.category_settings); frame(app);
        const ImGuiID child_id = ImHashStr("##category_group_items", 0, id.category_settings);
        ImGuiWindow* child = nullptr;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            if (window->ChildId == child_id) child = window;
        check(child != nullptr, "multiselect child visible");
        if (child) {
            auto click_row = [&](int row, bool ctrl, bool shift) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
                io.AddKeyEvent(ImGuiMod_Shift, shift);
                io.AddMousePosEvent(child->DC.CursorStartPos.x + 12.0f,
                                    child->DC.CursorStartPos.y + (row + 0.5f) * ImGui::GetTextLineHeightWithSpacing());
                frame(app);
                io.AddMouseButtonEvent(0, true); frame(app);
                io.AddMouseButtonEvent(0, false); frame(app);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddKeyEvent(ImGuiMod_Shift, false);
                frame(app);
            };
            click_row(1, false, false);
            click_row(3, true, false);
            const ImGuiID target = ImHashStr("\xe7\xa7\xbb\xe5\x8a\xa8\xe8\x87\xb3##category_group_target", 0, id.category_settings);
            select_combo_entry(app, target, 1, "Target");
            const ImGuiID move = ImHashStr("\xe7\xa7\xbb\xe5\x8a\xa8\xe5\xb0\x8f\xe7\xb1\xbb##category_group", 0, id.category_settings);
            activate(app, move);
            auto saved = repo.load();
            check(saved.customized_category_groups[0].categories == std::vector<std::string>({"A", "C", "E"}) &&
                  saved.customized_category_groups[1].categories == std::vector<std::string>({"X", "B", "D"}),
                  "mouse ctrl selection moves both categories");
            click_row(2, false, false);
            click_row(1, false, true);
            const ImGuiID up = ImHashStr("\xe5\xb0\x8f\xe7\xb1\xbb\xe4\xb8\x8a\xe7\xa7\xbb##category_group", 0, id.category_settings);
            const ImGuiID down = ImHashStr("\xe5\xb0\x8f\xe7\xb1\xbb\xe4\xb8\x8b\xe7\xa7\xbb##category_group", 0, id.category_settings);
            activate(app, up);
            check(repo.load().customized_category_groups[1].categories == std::vector<std::string>({"B", "D", "X"}),
                  "mouse reverse shift selection reorders both up");
            activate(app, down);
            check(repo.load().customized_category_groups[1].categories == std::vector<std::string>({"X", "B", "D"}),
                  "selection remains after reorder and moves down");
        }
        app.shutdown();
    }
    ImGui::DestroyContext();
}
void test_right_click(th::SettingsRepository& repo) {
    std::string error;
    th::FavlistRepository favourites("favlist.fav");
    const th::Position destination{10, 20, 30, std::nullopt, 1};
    check(favourites.save({{"Test-entry", destination}}, error) == 1, "right-click fixture saves");
    th::Settings settings;
    settings.default_version = "1.12.3";
    settings.teleport_api_enabled = true;
    check(repo.save(settings, error), "right-click settings fixture saves");
    create_imgui();
    {
        auto fake = std::make_unique<FakeMemory>();
        FakeMemory* memory = fake.get();
        memory->prepare(*th::find_version("1.12.3"));
        std::vector<std::string> commands;
        th::App app;
        app.initialize(nullptr, th::BackendMode::InProcess, std::move(fake));
        app.set_api_sender([&](const std::string& command, std::string&) {
            commands.push_back(command); return true;
        });
        frame(app); frame(app);
        const Ids id = ids();
        const ImGuiID table_id = ImHashStr("favourites", 0, id.points);
        ImGuiTable* table = ImGui::GetCurrentContext()->Tables.GetByKey(table_id);
        check(table && table->RowPosY2 > table->RowPosY1, "favourite row geometry is available");
        if (table && table->RowPosY2 > table->RowPosY1) {
            const float x = (table->Columns[0].MinX + table->Columns[0].MaxX) * 0.5f;
            const float coordinate_x = (table->Columns[1].MinX + table->Columns[1].MaxX) * 0.5f;
            const float y = (table->RowPosY1 + table->RowPosY2) * 0.5f;
            ImGuiIO& io = ImGui::GetIO();
            const auto click = [&](int button, float mx, float my) {
                io.AddMousePosEvent(mx, my); frame(app);
                io.AddMouseButtonEvent(button, true); frame(app);
                io.AddMouseButtonEvent(button, false); frame(app);
            };
            click(1, x, y);
            check(commands.empty() && memory->writes == 0, "right-click does nothing while disabled");
            activate(app, id.right_click);
            check(repo.load().right_click_teleport, "right-click checkbox saves immediately");
            click(1, 1100, 900);
            check(commands.empty() && memory->writes == 0, "right-click outside favourites does nothing");
            io.AddMousePosEvent(x, y); frame(app);
            io.AddMouseButtonEvent(1, true); frame(app); frame(app); frame(app);
            io.AddMouseButtonEvent(1, false); frame(app);
            check(commands.size() == 1 && commands[0].find(" 1 10.000000 20.000000 30.000000 ") != std::string::npos,
                  "cross-map right-click sends one API command even while held");
            memory->current_map = 1;
            click(1, coordinate_x, y);
            check(commands.size() == 1 && memory->writes == 3,
                  "right-click in the coordinate column uses the existing same-map teleport");
            activate(app, id.right_click);
            check(!repo.load().right_click_teleport, "disabling right-click checkbox saves immediately");
            click(1, x, y);
            check(commands.size() == 1 && memory->writes == 3, "disabled right-click stops teleporting");
            click(0, x, y);
            click(0, x, y);
            check(memory->writes == 6, "left double-click still uses the existing teleport path");
        }
        app.shutdown();
    }
    ImGui::DestroyContext();
}
int run_fixture() {
    std::string error;
    th::FavlistRepository favourites("favlist.fav");
    check(favourites.save({{dungeon + "-entry", {1, 2, 3}},
                           {profession + "-entry", {4, 5, 6}}}, error) == 2,
          "isolated favourites fixture saves");
    th::SettingsRepository repo("settings.json");
    th::Settings settings;
    settings.last_category = dungeon;
    settings.global_search = "global";
    check(repo.save(settings, error), "isolated settings fixture saves");
    create_imgui();
    {
        th::App app;
        app.initialize(nullptr, th::BackendMode::InProcess);
        frame(app); frame(app);
        const Ids id = ids();
        check(focus_search(app, id.input) == "global", "startup restores global search");
        activate(app, id.independent);
        check(focus_search(app, id.input).empty(), "new independent category starts empty");
        ImGui::GetIO().AddInputCharactersUTF8(scarlet.c_str());
        frame(app);
        choose_category(app, id, profession);
        auto saved = repo.load();
        check(saved.independent_search && saved.last_category == profession &&
                  saved.category_searches[dungeon] == scarlet,
              "switching categories saves the unfinished dungeon search");
        check(focus_search(app, id.input).empty(), "profession starts with an empty search");
        ImGui::GetIO().AddInputCharactersUTF8("alchemy");
        frame(app);
        choose_category(app, id, dungeon);
        check(focus_search(app, id.input) == scarlet, "returning to dungeon restores its search");
        saved = repo.load();
        check(saved.category_searches[profession] == "alchemy", "profession search is persisted independently");
        activate(app, id.api);
        check(repo.load().teleport_api_enabled, "teleport API checkbox is persisted");
        app.shutdown();
    }
    ImGui::DestroyContext();
    create_imgui();
    {
        th::App app;
        app.initialize(nullptr, th::BackendMode::InProcess);
        frame(app); frame(app);
        const Ids id = ids();
        check(focus_search(app, id.input) == scarlet, "restart restores selected category search");
        activate(app, id.api);
        check(!repo.load().teleport_api_enabled, "restart restores checked API state before toggling off");
        activate(app, id.independent);
        check(focus_search(app, id.input) == "global", "disabling independent mode restores global search");
        activate(app, id.independent);
        check(focus_search(app, id.input) == scarlet, "enabling independent mode restores category search");
        app.shutdown();
    }
    ImGui::DestroyContext();
    test_right_click(repo);
    test_category_settings(repo);
    test_category_multiselect(repo);
    return failures ? 1 : 0;
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--fixture") == 0) return run_fixture();
    const auto dir = std::filesystem::temp_directory_path() /
        ("teleporthackdx-search-scope-ui-" + std::to_string(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(dir)) return 2;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return 3;
    const auto child_path = dir / "search_scope_ui_fixture.exe";
    if (!CopyFileW(path, child_path.c_str(), TRUE)) return 4;
    std::wstring command = L"\"" + child_path.wstring() + L"\" --fixture";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(child_path.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        dir.c_str(), &startup, &process)) return 5;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return static_cast<int>(result);
}
