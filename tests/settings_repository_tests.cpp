// settings.json compatibility tests, including separate DLL/desktop ImGui
// layout snapshots stored as escaped multiline JSON strings.
#include <filesystem>
#include <iostream>
#include <string>

#include "imgui.h"
#include "repository/settings_repository.h"

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
    settings.dll_imgui_layout = dll_layout;
    settings.desktop_imgui_layout = desktop_layout;

    std::string error;
    check(repo.save(settings, error), "settings with two ImGui layouts save");

    th::Settings loaded = repo.load();
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

    loaded.dll_imgui_layout += "Column 2  Width=24\n";
    check(repo.save(loaded, error), "updated DLL layout saves");
    th::Settings reloaded = repo.load();
    check(reloaded.dll_imgui_layout == loaded.dll_imgui_layout,
          "updated DLL layout is persisted");
    check(reloaded.desktop_imgui_layout == desktop_layout,
          "updating DLL layout preserves desktop layout");

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
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
