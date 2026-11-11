// settings.json compatibility tests, including separate DLL/desktop ImGui
// layout snapshots stored as escaped multiline JSON strings.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "imgui.h"
#include "repository/settings_repository.h"
#include "ui/theme.h"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "teleporthackdx-settings-tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "settings.json";

    const std::string dll_layout =
        "[Window][TeleportHack DX]\n"
        "Pos=123,45\n"
        "Size=640,820\n\n"
        "[Table][0x11223344,5]\n"
        "Column 0  Width=500\n"
        "Column 1  Width=24\n";
    const std::string desktop_layout =
        "[Window][TeleportHack DX (Desktop / Attach)]\n"
        "Pos=0,0\n"
        "Size=500,900\n"
        "Label=quoted \"text\" and C:\\layout\n";

    th::SettingsRepository repo(path);
    th::Settings settings;
    settings.default_version = "3.3.5";
    settings.wow_exe_path = "C:\\Games\\WoW 3.3.5\\WoW.exe";
    settings.desktop_always_on_top = true;
    settings.movement_speed_value = 25.0;
    settings.winforms_window_x = -1200;
    settings.winforms_window_saved = true;
    settings.winforms_always_on_top = true;
    settings.wpf_window_x = -900.5;
    settings.wpf_window_saved = true;
    settings.wpf_always_on_top = true;
    settings.pid_poll_seconds = 45;
    settings.auto_save_favourites = true;
    settings.teleport_api_enabled = true;
    settings.right_click_teleport = true;
    settings.independent_search = true;
    settings.global_search = "global \\\"query\\\"";
    settings.category_searches = {{"\xe5\x89\xaf\xe6\x9c\xac" "60", "\xe8\xa1\x80\xe8\x89\xb2"},
                                  {"quoted \\\"category\\\"", "smith"}};
    settings.search_history = {"\xe6\x9a\xb4\xe9\xa3\x8e\xe5\x9f\x8e", "quote \"x\" \\ path", "  spaced  "};
    settings.collapse_hotkey = VK_F8;
    settings.collapse_hotkey_ctrl = true;
    settings.collapse_hotkey_alt = true;
    settings.interface_opacity = 65;
    settings.favourite_list_rows = 21;
    settings.show_favourite_columns = {true, false, true, false, false};
    settings.imgui_theme = "light";
    settings.crud_button_width = 142;
    settings.crud_button_height = 48;
    settings.crud_button_spacing = 13;
    settings.dll_imgui_layout = dll_layout;
    settings.desktop_imgui_layout = desktop_layout;

    std::string error;
    check(repo.save(settings, error), "settings with two ImGui layouts save");

    th::Settings loaded = repo.load();
    check(loaded.auto_save_favourites, "auto-save enabled survives save and reload");
    check(loaded.teleport_api_enabled && loaded.right_click_teleport && loaded.independent_search && loaded.global_search == settings.global_search &&
              loaded.category_searches == settings.category_searches,
          "teleport API and independent category searches survive save and reload");
    check(loaded.search_history == settings.search_history,
          "search history preserves order, UTF-8, quotes, backslashes, and spaces");
    check(loaded.collapse_hotkey == VK_F8 && loaded.collapse_hotkey_ctrl &&
              !loaded.collapse_hotkey_shift && loaded.collapse_hotkey_alt,
          "collapse shortcut survives save and reload");
    check(loaded.interface_opacity == 65 && loaded.favourite_list_rows == 21 &&
              loaded.show_favourite_columns == settings.show_favourite_columns,
          "display settings and individual columns survive save and reload");
    check(loaded.imgui_theme == "light", "light theme survives save and reload");
    check(loaded.crud_button_width == 142 && loaded.crud_button_height == 48 &&
              loaded.crud_button_spacing == 13, "button dimensions survive save and reload");
    check(loaded.pid_poll_seconds == 45, "WoW PID poll interval survives save and reload");
    check(loaded.dll_imgui_layout == dll_layout,
          "DLL ImGui layout survives JSON escaping and reload");
    check(loaded.desktop_imgui_layout == desktop_layout,
          "desktop ImGui layout survives quotes, backslashes, and reload");
    check(loaded.wow_exe_path == settings.wow_exe_path,
          "launcher WoW.exe path survives JSON escaping and reload");
    check(loaded.desktop_always_on_top,
          "desktop always-on-top preference survives save and reload");
    check(loaded.movement_speed_value == 25.0,
          "movement speed value survives save and reload");
    check(loaded.winforms_window_saved && loaded.winforms_window_x == -1200 &&
              loaded.winforms_always_on_top,
          "WinForms window state survives native save and reload");
    check(loaded.wpf_window_saved && loaded.wpf_window_x == -900.5 &&
              loaded.wpf_always_on_top,
          "WPF window state survives native save and reload");
    check(loaded.dll_imgui_layout != loaded.desktop_imgui_layout,
          "DLL and desktop layouts remain independent");

    loaded.imgui_theme = "dark";
    loaded.auto_save_favourites = false;
    check(repo.save(loaded, error), "dark theme saves");
    check(!repo.load().auto_save_favourites, "auto-save disabled survives save and reload");
    check(repo.load().imgui_theme == "dark", "dark theme survives save and reload");
    loaded.imgui_theme = "light";
    loaded.dll_imgui_layout += "Column 2  Width=24\n";
    check(repo.save(loaded, error), "updated DLL layout saves");
    th::Settings reloaded = repo.load();
    check(reloaded.search_history == settings.search_history, "unrelated settings updates preserve search history");
    check(reloaded.dll_imgui_layout == loaded.dll_imgui_layout,
          "updated DLL layout is persisted");
    check(reloaded.desktop_imgui_layout == desktop_layout,
          "updating DLL layout preserves desktop layout");
    check(reloaded.imgui_theme == "light", "updating layout preserves the chosen theme");
    check(reloaded.crud_button_width == 142 && reloaded.crud_button_height == 48 &&
              reloaded.crud_button_spacing == 13, "layout updates preserve button dimensions");

    // Out-of-range intervals (hand-edited JSON, older C# builds) fall back
    // to the 30 s default like ParseWowPidPollSeconds().
    {
        const std::filesystem::path bad = dir / "bad-poll.json";
        std::ofstream out(bad, std::ios::binary);
        out << "{\n  \"pid_poll_seconds\": 0\n}\n";
    }
    check(th::SettingsRepository(dir / "bad-poll.json").load().pid_poll_seconds == 30,
          "invalid PID poll interval falls back to 30 s");
    check(th::Settings{}.pid_poll_seconds == 30, "default PID poll interval is 30 s");
    check(th::Settings{}.imgui_theme == "dark", "older settings default to dark");
    check(!th::SettingsRepository(dir / "bad-poll.json").load().auto_save_favourites,
          "settings without auto-save default to disabled");
    const th::Settings old_settings = th::SettingsRepository(dir / "bad-poll.json").load();
    check(!old_settings.teleport_api_enabled && !old_settings.right_click_teleport && !old_settings.independent_search &&
              old_settings.global_search.empty() && old_settings.category_searches.empty(),
          "older settings default to disabled API and global search");
    loaded.right_click_teleport = false;
    check(repo.save(loaded, error) && !repo.load().right_click_teleport,
          "disabled right-click teleport survives save and reload");
    check(old_settings.search_history.empty(), "older settings have empty search history");
    {
        std::ofstream out(dir / "history-corrupt.json", std::ios::binary);
        out << "{\"search_history_count\":99999999,\"search_history_0\":\"valid\","
               "\"search_history_1\":\"valid\",\"search_history_2\":\"   \"}";
    }
    check(th::SettingsRepository(dir / "history-corrupt.json").load().search_history ==
              std::vector<std::string>{"valid"}, "malformed history is bounded and duplicate/empty entries are ignored");
    check(old_settings.collapse_hotkey == 0, "collapse shortcut defaults to disabled");
    {
        std::ofstream out(dir / "bad-shortcut.json", std::ios::binary);
        out << "{\"collapse_hotkey\":999}";
    }
    check(th::SettingsRepository(dir / "bad-shortcut.json").load().collapse_hotkey == 0,
          "unsupported collapse shortcut is disabled");
    check(th::sanitize_collapse_hotkey('A') == 'A' && th::sanitize_collapse_hotkey('9') == '9' &&
              th::sanitize_collapse_hotkey(VK_F12) == VK_F12 &&
              th::sanitize_collapse_hotkey(VK_INSERT) == VK_INSERT &&
              th::sanitize_collapse_hotkey(VK_ESCAPE) == 0,
          "shortcut keys accept supported ranges and reject unsupported keys");
    check(old_settings.interface_opacity == 100 && old_settings.favourite_list_rows == 13 &&
              old_settings.show_favourite_columns == std::array<bool, 5>{true, true, true, true, true},
          "older settings retain the original display defaults");
    {
        std::ofstream out(dir / "legacy-columns.json", std::ios::binary);
        out << "{\"show_favourite_coordinates\":false,\"show_favourite_y\":true}";
    }
    th::Settings migrated = th::SettingsRepository(dir / "legacy-columns.json").load();
    check(migrated.show_favourite_columns == std::array<bool, 5>{false, true, false, false, false},
          "legacy switch migrates and individual settings override it");
    th::SettingsRepository migrated_repo(dir / "legacy-columns.json");
    check(migrated_repo.save(migrated, error) &&
              migrated_repo.load().show_favourite_columns == migrated.show_favourite_columns,
          "migrated columns remain independent after save and reload");
    {
        std::ofstream out(dir / "legacy-columns-true.json", std::ios::binary);
        out << "{\"show_favourite_coordinates\":true}";
    }
    check(th::SettingsRepository(dir / "legacy-columns-true.json").load().show_favourite_columns ==
              std::array<bool, 5>{true, true, true, true, true}, "legacy enabled switch shows all columns");
    {
        std::ofstream out(dir / "bad-display.json", std::ios::binary);
        out << "{\"interface_opacity\":-1,\"favourite_list_rows\":9999}";
    }
    const th::Settings safe_display = th::SettingsRepository(dir / "bad-display.json").load();
    check(safe_display.interface_opacity == th::kMinInterfaceOpacity &&
              safe_display.favourite_list_rows == th::kMaxFavouriteRows,
          "out-of-range display settings are clamped");
    check(th::Settings{}.crud_button_width == th::kDefaultCrudButtonWidth &&
              th::Settings{}.crud_button_height == th::kDefaultCrudButtonHeight &&
              th::Settings{}.crud_button_spacing == th::kDefaultCrudButtonSpacing,
          "older settings get valid button defaults");
    {
        std::ofstream out(dir / "bad-buttons.json", std::ios::binary);
        out << "{\"crud_button_width\":9999,\"crud_button_height\":-2,\"crud_button_spacing\":9999}";
    }
    const th::Settings safe_buttons = th::SettingsRepository(dir / "bad-buttons.json").load();
    check(safe_buttons.crud_button_width == th::kMaxCrudButtonWidth &&
              safe_buttons.crud_button_height == th::kMinCrudButtonHeight &&
              safe_buttons.crud_button_spacing == th::kMaxCrudButtonSpacing,
          "out-of-range button dimensions are clamped");
    {
        std::ofstream out(dir / "bad-theme.json", std::ios::binary);
        out << "{\"imgui_theme\":\"unexpected\"}";
    }
    check(th::SettingsRepository(dir / "bad-theme.json").load().imgui_theme == "dark",
          "unknown theme falls back to dark");

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    check(th::apply_interface_opacity(65, nullptr, false) &&
              ImGui::GetStyle().Alpha > 0.649f && ImGui::GetStyle().Alpha < 0.651f,
          "injected mode applies opacity to all ImGui controls");
    HWND test_window = CreateWindowExA(0, "STATIC", "opacity test", WS_OVERLAPPED,
                                       0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(test_window != nullptr, "native test window is created");
    if (test_window) {
        BYTE alpha = 0;
        DWORD flags = 0;
        check(th::apply_interface_opacity(65, test_window, true) &&
                  (GetWindowLongPtrW(test_window, GWL_EXSTYLE) & WS_EX_LAYERED) &&
                  GetLayeredWindowAttributes(test_window, nullptr, &alpha, &flags) &&
                  (flags & LWA_ALPHA) && alpha == 166 && ImGui::GetStyle().Alpha == 1.0f,
              "desktop mode applies native window opacity without double fading");
        check(th::apply_interface_opacity(100, test_window, true) &&
                  !(GetWindowLongPtrW(test_window, GWL_EXSTYLE) & WS_EX_LAYERED),
              "full opacity restores the desktop window style");
        DestroyWindow(test_window);
    }
    ImGui::GetStyle().FramePadding = ImVec2(17, 19);
    th::apply_imgui_theme("light");
    const ImVec4 light_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    const ImVec4 light_text = ImGui::GetStyle().Colors[ImGuiCol_Text];
    check(light_bg.x > 0.8f && light_text.x < 0.2f, "light theme uses dark text on a light background");
    th::apply_imgui_theme("dark");
    const ImVec4 dark_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    const ImVec4 dark_text = ImGui::GetStyle().Colors[ImGuiCol_Text];
    check(dark_bg.x < 0.2f && dark_text.x > 0.8f, "dark theme uses light text on a dark background");
    check(ImGui::GetStyle().FramePadding.x == 17 && ImGui::GetStyle().FramePadding.y == 19,
          "switching themes preserves layout metrics");
    ImGui::LoadIniSettingsFromMemory(reloaded.dll_imgui_layout.data(),
                                     reloaded.dll_imgui_layout.size());
    size_t exported_size = 0;
    const char* exported_data = ImGui::SaveIniSettingsToMemory(&exported_size);
    const std::string exported(exported_data, exported_size);
    check(exported.find("[Window][TeleportHack DX]") != std::string::npos,
          "vendored ImGui loads and exports the in-memory DLL layout");
    ImGui::DestroyContext();

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "settings repository tests passed\n";
    return 0;
}
