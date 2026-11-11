#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "repository/settings_repository.h"
#include "ui/app.h"

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void frame(th::App& app) {
    ImGui::NewFrame();
    app.render();
    ImGui::Render();
}
}

int main() {
    const auto original = std::filesystem::current_path();
    const auto dir = std::filesystem::temp_directory_path() /
        ("teleporthackdx-search-ui-" + std::to_string(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(dir)) return 2;
    std::filesystem::current_path(dir);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1200, 1000);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    th::App app;
    frame(app);
    frame(app);
    ImGuiWindow* window = ImGui::FindWindowByName("TeleportHack DX");
    check(window != nullptr, "main window exists");
    if (window) {
        const ImGuiID bar = window->GetID("##main_tabs");
        const ImGuiID points = ImHashStr("\xe5\x9d\x90\xe6\xa0\x87\xe7\x82\xb9", 0, bar);
        const ImGuiID input = ImHashStr("##search", 0, points);
        ImGui::ActivateItemByID(input);
        frame(app);
        check(ImGui::GetCurrentContext()->ActiveId == input, "search input receives focus");
        io.AddInputCharactersUTF8("stormwind");
        frame(app);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame(app);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame(app);
        th::SettingsRepository repo(dir / "settings.json");
        check(repo.load().search_history == std::vector<std::string>{"stormwind"},
              "typing in the real search box and pressing Enter saves history");
        ImGui::ActivateItemByID(input);
        frame(app);
        io.AddInputCharactersUTF8("harbor");
        frame(app);
        app.finish_search_edit();
        const auto entries = repo.load().search_history;
        if (entries.size() != 2 || entries[0] != "harbor" || entries[1] != "stormwind") {
            for (const auto& entry : entries) std::cerr << "history entry: [" << entry << "]\n";
        }
        check(entries.size() == 2 && entries[0] == "harbor" && entries[1] == "stormwind",
              "unfinished real input is committed when the interface is hidden");

        const ImGuiID settings_tab = ImHashStr("\xe8\xae\xbe\xe7\xbd\xae", 0, bar);
        ImGui::ActivateItemByID(settings_tab);
        frame(app);
        frame(app);
        const ImGuiID history_input = ImHashStr("##history_entry", 0, settings_tab);
        ImGui::ActivateItemByID(history_input);
        frame(app);
        check(ImGui::GetCurrentContext()->ActiveId == history_input, "history management input receives focus");
        io.AddInputCharactersUTF8("manual");
        frame(app);
        const ImGuiID add = ImHashStr("\xe6\x96\xb0\xe5\xa2\x9e##history", 0, settings_tab);
        ImGui::ActivateItemByID(add);
        frame(app);
        check(repo.load().search_history == std::vector<std::string>{"manual", "harbor", "stormwind"},
              "settings page adds history and persists it");
        ImGui::ActivateItemByID(history_input);
        frame(app);
        io.AddInputCharactersUTF8("edited");
        frame(app);
        const ImGuiID edit = ImHashStr("\xe4\xbf\x9d\xe5\xad\x98\xe4\xbf\xae\xe6\x94\xb9##history", 0, settings_tab);
        ImGui::ActivateItemByID(edit);
        frame(app);
        check(repo.load().search_history == std::vector<std::string>{"edited", "harbor", "stormwind"},
              "settings page edits selected history and persists it");
        const ImGuiID remove = ImHashStr("\xe5\x88\xa0\xe9\x99\xa4##history", 0, settings_tab);
        ImGui::ActivateItemByID(remove);
        frame(app);
        check(repo.load().search_history == std::vector<std::string>{"harbor", "stormwind"},
              "settings page deletes selected history and persists it");

        ImGui::ActivateItemByID(points);
        frame(app);
        frame(app);
        const ImGuiID combo = ImHashStr("##search_history", 0, points);
        ImGui::ActivateItemByID(combo);
        frame(app);
        ImGuiContext* context = ImGui::GetCurrentContext();
        check(context->OpenPopupStack.Size > 0 && context->OpenPopupStack.back().Window,
              "search history dropdown opens");
        if (context->OpenPopupStack.Size > 0 && context->OpenPopupStack.back().Window) {
            ImGuiWindow* popup = context->OpenPopupStack.back().Window;
            const int index = 1;
            const ImGuiID row_scope = ImHashData(&index, sizeof(index), popup->ID);
            const ImGuiID row = ImHashStr("stormwind", 0, row_scope);
            ImGui::ActivateItemByID(row);
            frame(app);
            check(repo.load().search_history == std::vector<std::string>{"stormwind", "harbor"},
                  "choosing an older history entry makes it most recent");
            ImGui::ActivateItemByID(input);
            frame(app);
            check(context->ActiveId == input && context->InputTextState.TextA.Data &&
                      std::string(context->InputTextState.TextA.Data) == "stormwind",
                  "choosing history loads it into the search input");

            const ImGuiID independent = ImHashStr("\xe7\x8b\xac\xe7\xab\x8b\xe6\x90\x9c\xe7\xb4\xa2", 0, points);
            ImGui::ActivateItemByID(independent);
            frame(app);
            check(repo.load().independent_search && repo.load().global_search == "stormwind",
                  "enabling independent search preserves the global query");
            ImGui::ActivateItemByID(input);
            frame(app);
            check(context->ActiveId == input && std::string(context->InputTextState.TextA.Data).empty(),
                  "new independent category starts with an empty search input");
            io.AddInputCharactersUTF8("local");
            frame(app);
            ImGui::ActivateItemByID(independent);
            frame(app);
            auto scope = repo.load();
            check(!scope.independent_search && scope.global_search == "stormwind" &&
                      scope.category_searches[th::ALL_CATEGORY] == "local",
                  "switching mode saves an uncommitted category query and restores global mode");
            ImGui::ActivateItemByID(input);
            frame(app);
            check(context->ActiveId == input && std::string(context->InputTextState.TextA.Data) == "stormwind",
                  "global query is restored to the actual search input");
            ImGui::ActivateItemByID(independent);
            frame(app);
            ImGui::ActivateItemByID(input);
            frame(app);
            check(context->ActiveId == input && std::string(context->InputTextState.TextA.Data) == "local",
                  "re-enabling independent mode restores category query to the input");

            const ImGuiID api = ImHashStr("\xe4\xbc\xa0\xe9\x80\x81" "api", 0, points);
            ImGui::ActivateItemByID(api);
            frame(app);
            check(repo.load().teleport_api_enabled, "checking teleport API persists immediately");
            ImGui::ActivateItemByID(api);
            frame(app);
            check(!repo.load().teleport_api_enabled, "unchecking teleport API persists immediately");
        }
    }
    ImGui::DestroyContext();
    std::filesystem::current_path(original);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return failures ? 1 : 0;
}
