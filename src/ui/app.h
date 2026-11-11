// Main overlay UI: owns every service and renders the ImGui frame that
// replaces TeleportHackOnVanilla's AutoIt/Qt window.
//
// Layout mirrors TeleportHackOnVanilla's tabbed AutoIt window (d25a20f):
// [process list, Attach mode only] -> client row -> tabs
//   坐标点   category/search, favourites table, description, CRUD rows,
//            最近使用的坐标, Teleport button
//   分类导航 grouped 3-column category grid (click -> back to 坐标点)
//   设置     step/patch toggles, speeds, PID polling, paths, import/export
//   关于     version, author, release notes
// -> log (always visible).
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
#include <utility>
#include <vector>

#include "domain/versions.h"
#include "memory/memory_backend.h"
#include "repository/settings_repository.h"
#include "services/favourites_porter.h"
#include "services/favourites_service.h"
#include "services/feature_service.h"
#include "services/hotkey_service.h"
#include "services/recent_teleports.h"
#include "services/teleport_service.h"
#include "ui/log_view.h"
#include "ui/collapse_shortcut.h"
#include "ui/category_selection.h"
#include "util/process_list.h"

namespace th {

enum class BackendMode {
    InProcess, // DLL injected into the game process itself
    Attach,    // standalone desktop app, attaches to a chosen WoW.exe PID
};

class App {
public:
    App();

    using ApiSender = std::function<bool(const std::string&, std::string&)>;
    void set_api_sender(ApiSender sender) { api_sender_ = std::move(sender); }

    // Called once the overlay's window handle is known (from the D3D9
    // device's creation parameters in the DLL, or our own window in the
    // desktop build). Loads settings/favourites/hotkeys and switches on
    // the configured/default version.
    // The optional backend replaces only the in-process backend, for isolated tests.
    void initialize(HWND owner_hwnd, BackendMode mode,
                    std::unique_ptr<MemoryBackend> inprocess_backend = nullptr);

    // Called every frame between ImGui::NewFrame() and ImGui::Render(),
    // only while the overlay is visible.
    void render();

    // Called every frame regardless of overlay visibility, to advance any
    // in-flight step-teleport.
    void tick();
    void finish_search_edit();

    // Forward WM_HOTKEY from the hooked/owned WndProc. Returns true if handled.
    bool on_wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
    bool on_collapse_shortcut(UINT msg, WPARAM wParam, LPARAM lParam, bool overlay_visible);

    // Desktop build only: called by main.cpp with the OS window's current
    // screen rect (e.g. from GetWindowRect) right before shutdown(), so
    // the saved settings.json remembers where the user left the window.
    // The injected overlay's bounds and table columns are persisted in its
    // own ImGui layout field inside the shared settings.json.
    void set_window_bounds(int x, int y, int width, int height);

    // Persist settings + unregister hotkeys; call before tearing down
    // ImGui/the hook/the window.
    void shutdown();

private:
    bool capture_imgui_layout();

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
    RecentTeleports recent_;

    LogView log_;

    // ---- UI state ----
    bool step_enabled_ = false;
    CollapseShortcut collapse_shortcut_;
    std::atomic<bool> overlay_text_input_{false};
    uint32_t teleport_api_token_ = 0;
    ApiSender api_sender_;
    std::string selected_category_ = ALL_CATEGORY;
    char search_buf_[128] = {0};
    bool search_edit_dirty_ = false;
    int history_manager_selected_ = -1;
    char history_manager_buf_[128] = {0};
    int category_group_selected_ = 0;
    int category_group_target_ = 0;
    bool category_group_initialized_ = false;
    char category_group_name_buf_[128] = {0};
    CategorySelection category_group_selection_;
    std::string category_group_message_;
    char desc_buf_[256] = {0};
    char speed_buf_[32] = "7";
    char movement_speed_buf_[32] = "7";
    bool movement_speed_enabled_ = false;
    int selected_row_ = -1; // index within the currently *displayed* rows
    char import_path_buf_[512] = {0};
    char export_path_buf_[512] = {0};
    double next_layout_snapshot_time_ = 0.0;
    // 分类导航 -> 坐标点: the tab switch is applied on the next frame.
    bool focus_points_tab_ = false;

    // Attach-mode only: process picker state.
    std::vector<ProcessEntry> process_list_;
    unsigned long selected_pid_ = 0;
    int pid_poll_input_ = kDefaultPidPollSeconds; // 设置 tab edit buffer
    double next_pid_poll_time_ = 0.0;             // ImGui::GetTime() seconds

    // ---- helpers ----
    void switch_version(const std::string& name);
    void reload_favourites();
    void reload_hotkeys();
    void log(const std::string& msg);
    void reload_process_list();
    void attach_to_pid(unsigned long pid);
    // TeleportHackOnVanilla's SelectOnlyWowPid()/CheckWowPidForReload():
    // refreshes the process list and, when exactly one WoW.exe runs and it
    // isn't the attached PID, attaches to it. Attach mode only.
    void auto_attach_single_process(const char* reason);
    bool set_desktop_always_on_top(bool enabled);
    void select_category(const std::string& name);
    void restore_search_scope();
    void configure_collapse_shortcut();

    std::vector<TeleportPoint> displayed_points() const;
    std::optional<TeleportPoint> current_point() const;

    // Map guard -> instant or step teleport -> 最近使用的坐标 entry.
    void do_teleport(const Position& pos, const std::string& label);
    void do_api_teleport(const Position& pos, const std::string& label);
    void do_favourite_teleport(const Position& pos, const std::string& label);
    void safe_toggle(const std::string& name,
                      const std::function<bool(bool&)>& fn);

    // ImGui section renderers
    void render_process_list();
    void render_version_row();
    void render_points_tab();
    void render_navigation_tab();
    void render_category_settings_tab();
    void render_settings_tab();
    void render_about_tab();
    void render_search();
    void render_search_history_settings();
    void render_table();
    void render_recent();
    void render_desc_input();
    void render_toggles_row();
    void render_crud_rows();
    void render_teleport_button();
    void render_speed_row();
    void render_pid_poll_row();
    void render_paths();
    void render_import_export();
    void render_log();
};

} // namespace th
