// Main in-game overlay: owns every service and renders the ImGui frame
// that replaces TeleportHackOnVanilla's AutoIt/Qt window.
//
// Layout intentionally mirrors
// TeleportHackOnVanilla/win/src/teleport_hack/presentation/main_window.py:
// category combo -> search -> favourites table -> description input ->
// toggles row -> CRUD rows -> Teleport button -> speed row -> log.
//
// The "WoW 进程" (attach to PID) section from the Python/AutoIt tools is
// dropped: this DLL always operates on the process it's injected into.
#pragma once

#include <Windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "domain/versions.h"
#include "memory/memory_backend.h"
#include "repository/settings_repository.h"
#include "services/favourites_porter.h"
#include "services/favourites_service.h"
#include "services/feature_service.h"
#include "services/hotkey_service.h"
#include "services/teleport_service.h"
#include "ui/log_view.h"

namespace th {

class App {
public:
    App();

    // Called once the game window handle is known (from the D3D9 device's
    // creation parameters). Loads settings/favourites/hotkeys and switches
    // on the configured/default version.
    void initialize(HWND game_hwnd);

    // Called every frame between ImGui::NewFrame() and ImGui::Render() by
    // the D3D9 hook, only while the overlay is visible.
    void render();

    // Called every frame regardless of overlay visibility, to advance any
    // in-flight step-teleport.
    void tick();

    // Forward WM_HOTKEY from the hooked WndProc. Returns true if handled.
    bool on_wndproc(UINT msg, WPARAM wParam, LPARAM lParam);

    // Persist settings + unregister hotkeys; call from DLL_PROCESS_DETACH /
    // before unhooking.
    void shutdown();

private:
    HWND hwnd_ = nullptr;
    Settings settings_;
    SettingsRepository settings_repo_;

    const GameVersion* version_ = nullptr;
    InProcessBackend backend_;
    std::optional<TeleportService> teleport_;
    std::optional<FeatureService> features_;
    TeleportStepSession step_session_;

    FavouritesService favourites_;
    FavouritesPorter porter_;
    std::optional<HotkeyService> hotkeys_;

    LogView log_;

    // ---- UI state ----
    bool step_enabled_ = false;
    std::string selected_category_ = ALL_CATEGORY;
    char search_buf_[128] = {0};
    char desc_buf_[256] = {0};
    char speed_buf_[32] = "7";
    int selected_row_ = -1; // index within the currently *displayed* rows
    char import_path_buf_[512] = {0};
    char export_path_buf_[512] = {0};
    bool show_settings_ = false;

    // ---- helpers ----
    void switch_version(const std::string& name);
    void reload_favourites();
    void reload_hotkeys();
    void log(const std::string& msg);

    std::vector<TeleportPoint> displayed_points() const;
    std::optional<TeleportPoint> current_point() const;

    void do_teleport(const Position& pos);
    void safe_toggle(const std::string& name,
                      const std::function<bool(bool&)>& fn);

    // ImGui section renderers
    void render_version_row();
    void render_category_and_search();
    void render_table();
    void render_desc_input();
    void render_toggles_row();
    void render_crud_rows();
    void render_teleport_button();
    void render_speed_row();
    void render_import_export();
    void render_log();
    void render_settings_popup();
};

} // namespace th
