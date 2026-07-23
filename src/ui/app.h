// Main overlay UI: owns every service and renders the ImGui frame that
// replaces TeleportHackOnVanilla's AutoIt/Qt window.
//
// Layout intentionally mirrors
// TeleportHackOnVanilla/win/src/teleport_hack/presentation/main_window.py:
// [process list, Attach mode only] -> category combo -> search ->
// favourites table -> description input -> toggles row -> CRUD rows ->
// Teleport button -> speed row -> log.
//
// This class is shared verbatim between two executables (see
// CMakeLists.txt): the injected DirectX hook DLL (BackendMode::InProcess,
// operates on the process it's injected into) and the standalone desktop
// TeleportHackDesktop.exe (BackendMode::Attach, attaches to a separately
// running WoW.exe over ReadProcessMemory/WriteProcessMemory -- handy for
// iterating on/verifying the UI and favlist logic without re-injecting).
// The MemoryBackend abstraction is what makes this possible: every
// service only ever talks to a `MemoryBackend&`, never caring whether
// reads/writes are in-process pointer derefs or cross-process API calls.
#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
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
#include "util/process_list.h"

namespace th {

enum class BackendMode {
    InProcess, // DLL injected into the game process itself
    Attach,    // standalone desktop app, attaches to a chosen WoW.exe PID
};

class App {
public:
    App();

    // Called once the overlay's window handle is known (from the D3D9
    // device's creation parameters in the DLL, or our own window in the
    // desktop build). Loads settings/favourites/hotkeys and switches on
    // the configured/default version.
    void initialize(HWND owner_hwnd, BackendMode mode);

    // Called every frame between ImGui::NewFrame() and ImGui::Render(),
    // only while the overlay is visible.
    void render();

    // Called every frame regardless of overlay visibility, to advance any
    // in-flight step-teleport.
    void tick();

    // Forward WM_HOTKEY from the hooked/owned WndProc. Returns true if handled.
    bool on_wndproc(UINT msg, WPARAM wParam, LPARAM lParam);

    // Persist settings + unregister hotkeys; call before tearing down
    // ImGui/the hook/the window.
    void shutdown();

private:
    HWND hwnd_ = nullptr;
    BackendMode mode_ = BackendMode::InProcess;
    Settings settings_;
    SettingsRepository settings_repo_;

    const GameVersion* version_ = nullptr;
    std::unique_ptr<MemoryBackend> backend_;
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

    // Attach-mode only: process picker state.
    std::vector<ProcessEntry> process_list_;
    unsigned long selected_pid_ = 0;

    // ---- helpers ----
    void switch_version(const std::string& name);
    void reload_favourites();
    void reload_hotkeys();
    void log(const std::string& msg);
    void reload_process_list();
    void attach_to_pid(unsigned long pid);

    std::vector<TeleportPoint> displayed_points() const;
    std::optional<TeleportPoint> current_point() const;

    void do_teleport(const Position& pos);
    void safe_toggle(const std::string& name,
                      const std::function<bool(bool&)>& fn);

    // ImGui section renderers
    void render_process_list();
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
