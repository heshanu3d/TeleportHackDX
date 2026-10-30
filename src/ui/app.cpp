#include "ui/app.h"

#include <Windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include "imgui.h"
#include "domain/app_info.h"
#include "domain/category_groups.h"
#include "memory/outofprocess_backend.h"
#include "repository/hotkey_config_repository.h"
#include "repository/teleport_lua_repository.h"
#include "util/paths.h"

#pragma comment(lib, "comdlg32.lib")

namespace th {

namespace {
std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

// "(x, y, z[, r=..][, map=..])" for log lines.
std::string describe_position(const Position& pos) {
    char buf[192];
    int n = std::snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f", pos.x, pos.y, pos.z);
    if (n > 0 && pos.orientation.has_value() && static_cast<size_t>(n) < sizeof(buf)) {
        n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), ", r=%.3f",
                           *pos.orientation);
    }
    if (n > 0 && pos.map_id.has_value() && static_cast<size_t>(n) < sizeof(buf)) {
        n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), ", map=%u",
                           static_cast<unsigned>(*pos.map_id));
    }
    std::string text(buf);
    return text + ")";
}

// Runs a Win32 common file dialog. `save` selects Save-As vs Open. Writes
// the chosen path into `buf` (unchanged if the user cancels). Returns
// true if a path was chosen.
bool run_file_dialog(HWND owner, char* buf, size_t buf_size, bool save) {
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Favlist (*.fav)\0*.fav\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(buf_size);
    ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
}
} // namespace

App::App()
    : settings_repo_("settings.json"), // re-pointed in initialize()
      favourites_(FavlistRepository("favlist.fav")),
      porter_(favourites_) {}

void App::initialize(HWND owner_hwnd, BackendMode mode) {
    hwnd_ = owner_hwnd;
    mode_ = mode;
    backend_ = (mode_ == BackendMode::InProcess)
                   ? std::unique_ptr<MemoryBackend>(std::make_unique<InProcessBackend>())
                   : std::unique_ptr<MemoryBackend>(std::make_unique<OutOfProcessBackend>());

    settings_repo_ = SettingsRepository(resolve_path("settings.json"));
    settings_ = settings_repo_.load();

    const std::string& imgui_layout = mode_ == BackendMode::InProcess
                                          ? settings_.dll_imgui_layout
                                          : settings_.desktop_imgui_layout;
    if (ImGui::GetCurrentContext() && !imgui_layout.empty()) {
        ImGui::LoadIniSettingsFromMemory(imgui_layout.data(), imgui_layout.size());
    }

    std::snprintf(speed_buf_, sizeof(speed_buf_), "%.6g", settings_.speed_value);
    std::snprintf(movement_speed_buf_, sizeof(movement_speed_buf_), "%.6g",
                  settings_.movement_speed_value);
    selected_category_ = settings_.last_category.empty() ? ALL_CATEGORY : settings_.last_category;
    settings_.pid_poll_seconds = sanitize_pid_poll_seconds(settings_.pid_poll_seconds);
    pid_poll_input_ = settings_.pid_poll_seconds;

    // porter_ was bound to favourites_ once in App::App()'s initializer
    // list; reassigning favourites_'s *contents* below keeps that
    // reference valid (the object's address doesn't change).
    favourites_ = FavouritesService(FavlistRepository(resolve_path(settings_.favlist_path)));

    hotkeys_.emplace(hwnd_);

    switch_version(settings_.default_version);
    reload_favourites();
    reload_hotkeys();
    if (mode_ == BackendMode::Attach) {
        auto_attach_single_process("startup"); // also fills process_list_
        next_pid_poll_time_ = (ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0) +
                              settings_.pid_poll_seconds;
        if (settings_.desktop_always_on_top && !set_desktop_always_on_top(true)) {
            log("[error] cannot restore always-on-top state (GetLastError=" +
                std::to_string(GetLastError()) + ")");
        }
    }

    log("initialized (" + (version_ ? version_->name : std::string("no version")) + ")");
}

void App::switch_version(const std::string& name) {
    if (movement_speed_enabled_ && features_) {
        if (!features_->reset_movement_speed()) {
            log("[warn] failed to restore movement speed before profile switch: " +
                features_->last_error());
        }
        movement_speed_enabled_ = false;
    }
    const GameVersion* v = find_version(name);
    if (!v) v = &all_versions().front();
    version_ = v;
    teleport_.emplace(*backend_, *version_);
    features_.emplace(*backend_, *version_);
    settings_.default_version = version_->name;
    log("using client profile: " + version_->name);
}

void App::reload_process_list() {
    if (!version_) return;
    process_list_ = list_processes_by_name(version_->executable);
}

void App::auto_attach_single_process(const char* reason) {
    if (mode_ != BackendMode::Attach || !backend_) return;
    reload_process_list();
    if (process_list_.size() != 1) return;
    const unsigned long pid = process_list_.front().pid;
    auto* oop = static_cast<OutOfProcessBackend*>(backend_.get());
    if (oop->is_attached() && oop->pid() == pid) return;
    const std::string previous =
        oop->is_attached() ? std::to_string(oop->pid()) : std::string("none");
    log(std::string(reason) + ": single WoW process, PID " + previous + " -> " +
        std::to_string(pid));
    selected_pid_ = pid;
    attach_to_pid(pid);
}

void App::attach_to_pid(unsigned long pid) {
    if (mode_ != BackendMode::Attach) return;
    if (movement_speed_enabled_ && features_) {
        if (!features_->reset_movement_speed()) {
            log("[warn] failed to restore movement speed before attach: " +
                features_->last_error());
        }
        movement_speed_enabled_ = false;
    }
    auto* oop = static_cast<OutOfProcessBackend*>(backend_.get());
    if (oop->attach(pid)) {
        selected_pid_ = pid;
        log("attached to PID " + std::to_string(pid));
    } else {
        log("[error] failed to attach to PID " + std::to_string(pid) + " (GetLastError=" +
            std::to_string(GetLastError()) + ")");
    }
}

void App::reload_favourites() {
    std::string error;
    if (!favourites_.reload(error)) {
        log("[warn] favlist load: " + error);
    }
}

void App::reload_hotkeys() {
    if (!hotkeys_) return;
    HotkeyConfigRepository repo(resolve_path(settings_.hotkey_path));
    std::vector<HotkeyBinding> bindings;
    std::string error;
    if (!repo.load(bindings, error)) {
        log("[warn] hotkey config: " + error);
        return;
    }
    std::vector<std::string> warnings;
    int bound = hotkeys_->apply(bindings, favourites_, warnings);
    for (const auto& w : warnings) log("[warn] " + w);
    log("registered " + std::to_string(bound) + "/" + std::to_string(bindings.size()) +
        " hotkeys");
}

void App::log(const std::string& msg) { log_.add(msg); }

std::vector<TeleportPoint> App::displayed_points() const {
    return favourites_.points_in_category(selected_category_);
}

std::optional<TeleportPoint> App::current_point() const {
    auto pts = displayed_points();
    if (selected_row_ < 0 || selected_row_ >= static_cast<int>(pts.size())) return std::nullopt;
    return pts[selected_row_];
}

void App::select_category(const std::string& name) {
    if (selected_category_ != name) selected_row_ = -1;
    selected_category_ = name;
    settings_.last_category = name;
}

void App::do_teleport(const Position& pos, const std::string& label) {
    if (!teleport_) return;
    // TeleportHackOnVanilla CanTeleportToMap(): writing coordinates can't
    // change maps, so a point recorded on another map is refused up front.
    if (!teleport_->can_teleport_to_map(pos)) {
        log("[warn] teleport cancelled: " + teleport_->last_error());
        return;
    }
    const std::string name = label.empty() ? std::string() : " '" + label + "'";
    if (step_enabled_) {
        StepConfig cfg;
        cfg.distance_per_step = settings_.step_distance;
        cfg.sleep_between_steps_sec = settings_.step_sleep_ms / 1000.0;
        if (!step_session_.start(*teleport_, pos, cfg)) {
            log("[error] step-teleport failed: " + teleport_->last_error());
            return;
        }
        log("step-teleport started" + name + " " + describe_position(pos));
    } else {
        if (!teleport_->write_position(pos)) {
            log("[error] teleport failed: " + teleport_->last_error());
            return;
        }
        log("teleported to" + name + " " + describe_position(pos));
    }
    recent_.record(label, pos);
}

void App::safe_toggle(const std::string& name, const std::function<bool(bool&)>& fn) {
    bool now_on = false;
    if (fn(now_on)) {
        log(name + ": " + (now_on ? "ON" : "OFF"));
    } else if (features_) {
        log("[error] " + name + ": " + features_->last_error());
    }
}

void App::tick() {
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        const double now = ImGui::GetTime();
        if (io.WantSaveIniSettings || now >= next_layout_snapshot_time_) {
            next_layout_snapshot_time_ = now + 1.0;
            if (capture_imgui_layout()) {
                std::string error;
                if (!settings_repo_.save(settings_, error)) {
                    log("[error] settings save failed: " + error);
                }
            }
            io.WantSaveIniSettings = false;
        }
    }

    if (teleport_ && step_session_.active()) {
        step_session_.tick(*teleport_);
    }
    if (movement_speed_enabled_ && features_ &&
        !features_->set_movement_speed(settings_.movement_speed_value)) {
        movement_speed_enabled_ = false;
        log("[error] movement speed disabled: " + features_->last_error());
    }

    // Desktop build: re-check WoW.exe every pid_poll_seconds and follow a
    // restarted client (TeleportHackOnVanilla's WoW PID 查询间隔 timer).
    if (mode_ == BackendMode::Attach && ImGui::GetCurrentContext()) {
        const double now = ImGui::GetTime();
        if (now >= next_pid_poll_time_) {
            next_pid_poll_time_ = now + sanitize_pid_poll_seconds(settings_.pid_poll_seconds);
            auto_attach_single_process("WoW PID check");
        }
    }
}

bool App::on_wndproc(UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    if (msg == WM_HOTKEY && hotkeys_) {
        Position pos;
        std::string label;
        if (hotkeys_->resolve(static_cast<int>(wParam), pos, &label)) {
            do_teleport(pos, label);
            return true;
        }
    }
    return false;
}

void App::set_window_bounds(int x, int y, int width, int height) {
    if (mode_ != BackendMode::Attach) return;
    settings_.window_x = x;
    settings_.window_y = y;
    settings_.window_width = width;
    settings_.window_height = height;
}

bool App::capture_imgui_layout() {
    if (!ImGui::GetCurrentContext()) return false;

    size_t layout_size = 0;
    const char* layout = ImGui::SaveIniSettingsToMemory(&layout_size);
    std::string& target = mode_ == BackendMode::InProcess
                               ? settings_.dll_imgui_layout
                               : settings_.desktop_imgui_layout;
    std::string snapshot(layout, layout_size);
    if (target == snapshot) return false;
    target = std::move(snapshot);
    return true;
}

void App::shutdown() {
    if (movement_speed_enabled_ && features_) {
        features_->reset_movement_speed(); // best-effort during normal EXE/DLL teardown
        movement_speed_enabled_ = false;
    }
    capture_imgui_layout();
    std::string error;
    if (!settings_repo_.save(settings_, error)) {
        log("[error] settings save failed: " + error);
    }
    if (hotkeys_) hotkeys_->clear();
}

// -------------------------------------------------------------------
// Rendering

void App::render() {
    const char* title =
        mode_ == BackendMode::InProcess ? "TeleportHack DX" : "TeleportHack DX (Desktop / Attach)";

    if (mode_ == BackendMode::InProcess) {
        // The injected overlay's window and table layout use the DLL-only
        // snapshot inside settings.json. Shared window bounds below belong
        // only to the desktop build's native OS window.
        ImGui::SetNextWindowSize(ImVec2(480.0f, 860.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin(title);
    } else {
        // Desktop build: the *OS* window (see src/desktop/main.cpp) is
        // what gets resized/moved by the user and persisted; pin the
        // ImGui content to fill it exactly, single-window-app style.
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
    }

    if (mode_ == BackendMode::Attach) {
        render_process_list();
        ImGui::Separator();
    }
    render_version_row();

    if (ImGui::BeginTabBar("##main_tabs")) {
        const ImGuiTabItemFlags points_flags =
            focus_points_tab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        focus_points_tab_ = false;
        if (ImGui::BeginTabItem("\xe5\x9d\x90\xe6\xa0\x87\xe7\x82\xb9", nullptr, // 坐标点
                                points_flags)) {
            render_points_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("\xe5\x88\x86\xe7\xb1\xbb\xe5\xaf\xbc\xe8\x88\xaa")) { // 分类导航
            render_navigation_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("\xe8\xae\xbe\xe7\xbd\xae")) { // 设置
            render_settings_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("\xe5\x85\xb3\xe4\xba\x8e")) { // 关于
            render_about_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    render_log();

    ImGui::End();
}

void App::render_points_tab() {
    render_category_and_search();
    render_table();
    render_recent();
    render_desc_input();
    render_crud_rows();
    render_teleport_button();
}

void App::render_settings_tab() {
    // 传送设置
    ImGui::SeparatorText("\xe4\xbc\xa0\xe9\x80\x81\xe8\xae\xbe\xe7\xbd\xae");
    ImGui::Checkbox("step (\xe5\x88\x86\xe6\xad\xa5\xe4\xbc\xa0\xe9\x80\x81)", // 分步传送
                    &step_enabled_);
    ImGui::SetNextItemWidth(160);
    ImGui::InputDouble("Step distance", &settings_.step_distance);
    int sleep_ms = settings_.step_sleep_ms;
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputInt("Step sleep (ms)", &sleep_ms)) settings_.step_sleep_ms = sleep_ms;
    ImGui::Checkbox("Fast step (skip Left/Right taps)", &settings_.fast_step);

    ImGui::SeparatorText("\xe5\x8a\x9f\xe8\x83\xbd\xe5\xbc\x80\xe5\x85\xb3"); // 功能开关
    render_toggles_row();

    ImGui::SeparatorText("\xe9\x80\x9f\xe5\xba\xa6"); // 速度
    render_speed_row();

    if (mode_ == BackendMode::Attach) {
        ImGui::SeparatorText("WoW PID \xe6\x9f\xa5\xe8\xaf\xa2\xe9\x97\xb4\xe9\x9a\x94"); // 查询间隔
        render_pid_poll_row();
    }

    ImGui::SeparatorText("\xe6\x96\x87\xe4\xbb\xb6\xe8\xb7\xaf\xe5\xbe\x84"); // 文件路径
    render_paths();

    ImGui::SeparatorText("\xe5\xaf\xbc\xe5\x85\xa5 / \xe5\xaf\xbc\xe5\x87\xba"); // 导入 / 导出
    render_import_export();
}

void App::render_navigation_tab() {
    // Same buckets/order as TeleportHackOnVanilla's RefreshCategoryGrid():
    // 杂项 (with 所有 first), 东部王国, 卡利姆多, 燃烧的远征, 巫妖王之怒.
    const auto groups = group_categories(favourites_.category_names());
    ImGui::BeginChild("##navigation", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 28),
                      false);
    for (size_t g = 0; g < groups.size(); ++g) {
        const std::string header =
            "\xe3\x80\x90" + groups[g].name + "\xe3\x80\x91"; // 【...】
        ImGui::SeparatorText(header.c_str());
        if (groups[g].categories.empty()) continue;
        ImGui::PushID(static_cast<int>(g));
        if (ImGui::BeginTable("##grid", 3,
                              ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
            for (const auto& category : groups[g].categories) {
                ImGui::TableNextColumn();
                const bool selected = (category == selected_category_);
                if (ImGui::Selectable(category.c_str(), selected)) {
                    select_category(category);
                    focus_points_tab_ = true;
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void App::render_about_tab() {
    ImGui::Text("TeleportHack DX  %s", build_version().c_str());
    ImGui::Text("\xe4\xbd\x9c\xe8\x80\x85\xef\xbc\x9a%s", app_author().c_str()); // 作者：
    ImGui::SeparatorText("\xe7\x89\x88\xe6\x9c\xac\xe6\x9b\xb4\xe6\x96\xb0\xe6\x97\xa5\xe5\xbf\x97"); // 版本更新日志
    ImGui::BeginChild("##release_notes", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 24),
                      true);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(release_notes().c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void App::render_process_list() {
    bool attached = backend_ && backend_->is_attached();
    ImGui::Text("WoW \xe8\xbf\x9b\xe7\xa8\x8b:"); // WoW 进程:
    ImGui::SameLine();
    if (attached) {
        auto* oop = static_cast<OutOfProcessBackend*>(backend_.get());
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "attached (PID %lu)", oop->pid());
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "not attached");
    }
    ImGui::SameLine();
    // Refresh also auto-attaches when exactly one WoW.exe is running
    // (TeleportHackOnVanilla's SelectOnlyWowPid on ReloadUI).
    if (ImGui::SmallButton("Refresh")) auto_attach_single_process("refresh");
    ImGui::SameLine();
    const char* topmost_label =
        settings_.desktop_always_on_top ? "Always on top: ON" : "Always on top: OFF";
    if (ImGui::SmallButton(topmost_label)) {
        const bool enabled = !settings_.desktop_always_on_top;
        if (set_desktop_always_on_top(enabled)) {
            settings_.desktop_always_on_top = enabled;
            std::string error;
            if (!settings_repo_.save(settings_, error)) {
                log("[error] settings save failed: " + error);
            }
        } else {
            log("[error] cannot change always-on-top state (GetLastError=" +
                std::to_string(GetLastError()) + ")");
        }
    }

    ImGui::BeginChild("##proclist", ImVec2(0, 70), true);
    if (process_list_.empty()) {
        ImGui::TextDisabled("(no %s found)", version_ ? version_->executable.c_str() : "WoW.exe");
    }
    for (const auto& p : process_list_) {
        bool selected = (p.pid == selected_pid_);
        char label[64];
        std::snprintf(label, sizeof(label), "%lu  -  %s", p.pid, p.exe_name.c_str());
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            selected_pid_ = p.pid;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) attach_to_pid(p.pid);
        }
    }
    ImGui::EndChild();
    if (ImGui::SmallButton("Attach")) {
        if (selected_pid_ != 0) attach_to_pid(selected_pid_);
    }
}

bool App::set_desktop_always_on_top(bool enabled) {
    if (mode_ != BackendMode::Attach || !hwnd_) return false;
    return SetWindowPos(hwnd_, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

void App::render_version_row() {
    ImGui::TextUnformatted("client:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    std::string current = version_ ? version_->name : "";
    if (ImGui::BeginCombo("##version", current.c_str())) {
        for (const auto& v : all_versions()) {
            bool selected = (version_ == &v);
            if (ImGui::Selectable(v.name.c_str(), selected)) {
                switch_version(v.name);
                reload_hotkeys();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Current map (all three clients expose one now); hidden while it
    // can't be read (not attached / not in world yet).
    if (version_) {
        uint32_t map_id;
        if (version_->map_id && teleport_ && teleport_->read_map_id(map_id)) {
            ImGui::SameLine();
            ImGui::Text("map:%u", map_id);
        }
    }
}

void App::render_category_and_search() {
    ImGui::TextUnformatted("\xe5\x88\x86\xe7\xb1\xbb:"); // 分类:
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##category", selected_category_.c_str())) {
        bool all_selected = (selected_category_ == ALL_CATEGORY);
        if (ImGui::Selectable(ALL_CATEGORY.c_str(), all_selected)) {
            select_category(ALL_CATEGORY);
        }
        for (const auto& name : favourites_.category_names()) {
            bool selected = (selected_category_ == name);
            if (ImGui::Selectable(name.c_str(), selected)) {
                select_category(name);
            }
        }
        ImGui::EndCombo();
    }
    settings_.last_category = selected_category_;

    ImGui::TextUnformatted("\xe6\x90\x9c\xe7\xb4\xa2:"); // 搜索:
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "\xe6\x90\x9c\xe7\xb4\xa2\xe6\x8f\x8f\xe8\xbf\xb0...",
                              search_buf_, sizeof(search_buf_));
}

void App::render_table() {
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    ImVec2 outer_size(0.0f, ImGui::GetTextLineHeightWithSpacing() * 13);
    if (ImGui::BeginTable("favourites", 6, flags, outer_size)) {
        ImGui::TableSetupColumn("\xe6\x8f\x8f\xe8\xbf\xb0", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("r", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("map", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        auto points = displayed_points();
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const auto& pt = points[i];
            if (!contains_ci(pt.description, search_buf_)) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool selected = (selected_row_ == i);
            ImGuiSelectableFlags sel_flags =
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
            ImGui::PushID(i);
            if (ImGui::Selectable(pt.description.c_str(), selected, sel_flags)) {
                selected_row_ = i;
                std::snprintf(desc_buf_, sizeof(desc_buf_), "%s", pt.description.c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    do_teleport(pt.position, pt.description);
                }
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", pt.position.x);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", pt.position.y);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", pt.position.z);
            ImGui::TableSetColumnIndex(4);
            if (pt.position.orientation.has_value()) {
                ImGui::Text("%.3f", *pt.position.orientation);
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(5);
            if (pt.position.map_id.has_value()) {
                ImGui::Text("%u", static_cast<unsigned>(*pt.position.map_id));
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
}

void App::render_recent() {
    // 最近使用的坐标 -- 双击再次传送
    ImGui::TextUnformatted(
        "\xe6\x9c\x80\xe8\xbf\x91\xe4\xbd\xbf\xe7\x94\xa8\xe7\x9a\x84\xe5\x9d\x90\xe6\xa0\x87");
    ImGui::SameLine();
    ImGui::TextDisabled("(\xe5\x8f\x8c\xe5\x87\xbb\xe5\x86\x8d\xe6\xac\xa1\xe4\xbc\xa0\xe9\x80\x81)");

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    ImVec2 outer_size(0.0f, ImGui::GetTextLineHeightWithSpacing() * 5.5f);
    int clicked = -1;
    if (ImGui::BeginTable("recent", 5, flags, outer_size)) {
        ImGui::TableSetupColumn("\xe6\x9c\x80\xe8\xbf\x91\xe4\xbd\xbf\xe7\x94\xa8", // 最近使用
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("map", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        const auto& entries = recent_.entries();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const auto& entry = entries[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (ImGui::Selectable(entry.label.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                clicked = i;
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", entry.position.x);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", entry.position.y);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", entry.position.z);
            ImGui::TableSetColumnIndex(4);
            if (entry.position.map_id.has_value()) {
                ImGui::Text("%u", static_cast<unsigned>(*entry.position.map_id));
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
    // Teleport after the loop: do_teleport() reorders the recent list.
    RecentTeleport entry;
    if (clicked >= 0 && recent_.get(static_cast<size_t>(clicked), entry)) {
        do_teleport(entry.position, entry.label);
    }
}

void App::render_desc_input() {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##desc",
        "\xe4\xbc\xa0\xe9\x80\x81\xe7\x82\xb9\xe6\x8f\x8f\xe8\xbf\xb0\xef\xbc\x88\xe5\xa6\x82\xef"
        "\xbc\x9a\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-\xe5\x85\xa5\xe5\x8f\xa3\xef\xbc"
        "\x89", // 传送点描述（如：斯坦索姆-入口）
        desc_buf_, sizeof(desc_buf_));
}

void App::render_toggles_row() {
    if (ImGui::Button("AntiJump")) {
        safe_toggle("anti-jump", [&](bool& on) { return features_->toggle_anti_jump(on); });
    }
    ImGui::SameLine();
    if (ImGui::Button("AutoLoot")) {
        safe_toggle("autoloot", [&](bool& on) { return features_->toggle_autoloot(on); });
    }
    ImGui::SameLine();
    if (ImGui::Button("LuaUnlock")) {
        safe_toggle("lua-unlock", [&](bool& on) { return features_->toggle_lua_unlock(on); });
    }
    ImGui::SameLine();
    if (ImGui::Button("PatchLoot")) {
        safe_toggle("patch-loot", [&](bool& on) { return features_->toggle_patch_loot(on); });
    }
}

void App::render_crud_rows() {
    std::string error;

    if (ImGui::Button("\xe6\x8f\x92\xe5\x89\x8d")) { // 插前 (insert before)
        if (selected_row_ < 0) {
            log("[warn] no row selected for insert");
        } else {
            Position pos;
            if (teleport_ && teleport_->read_position(pos) && desc_buf_[0] != '\0') {
                TeleportPoint pt{desc_buf_, pos};
                if (favourites_.insert_in_category(selected_category_, selected_row_, pt, error))
                    log("inserted " + pt.description);
                else
                    log("[error] " + error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("\xe6\x8f\x92\xe5\x90\x8e")) { // 插后 (insert after)
        if (selected_row_ < 0) {
            log("[warn] no row selected for append");
        } else {
            Position pos;
            if (teleport_ && teleport_->read_position(pos) && desc_buf_[0] != '\0') {
                TeleportPoint pt{desc_buf_, pos};
                if (favourites_.append_in_category(selected_category_, selected_row_, pt, error))
                    log("appended " + pt.description);
                else
                    log("[error] " + error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("\xe7\xbc\x96\xe8\xbe\x91")) { // 编辑 (edit)
        if (selected_row_ >= 0 && teleport_) {
            Position pos;
            if (teleport_->read_position(pos)) {
                TeleportPoint pt{desc_buf_, pos};
                if (favourites_.replace_in_category(selected_category_, selected_row_, pt, error))
                    log("updated row " + std::to_string(selected_row_));
                else
                    log("[error] " + error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("\xe5\x88\xa0\xe9\x99\xa4")) { // 删除 (delete)
        if (selected_row_ >= 0) {
            TeleportPoint removed;
            if (favourites_.delete_in_category(selected_category_, selected_row_, removed, error)) {
                log("deleted " + removed.description);
                selected_row_ = -1;
            } else {
                log("[error] " + error);
            }
        }
    }
    ImGui::SameLine();
    // 读取到炉石 (read-to-hearthstone): same current-position read as 编辑
    // (Edit), but instead of updating our in-memory favourites_ list it
    // patches the matching entry's x/y/z/o directly inside the
    // server-side Teleport.lua's `local FAV` table -- see
    // repository/teleport_lua_repository.h for why this is a surgical
    // single-line edit rather than a full gen_fav.py-style regeneration.
    if (ImGui::Button("\xe8\xaf\xbb\xe5\x8f\x96\xe5\x88\xb0\xe7\x82\x89\xe7\x9f\xb3")) {
        if (selected_row_ < 0) {
            log("[warn] no row selected for \xe8\xaf\xbb\xe5\x8f\x96\xe5\x88\xb0\xe7\x82\x89\xe7\x9f\xb3");
        } else if (settings_.teleport_lua_path.empty()) {
            log("[warn] Teleport.lua path not configured (see Settings)");
        } else if (teleport_) {
            Position pos;
            if (teleport_->read_position(pos)) {
                auto pt = current_point();
                if (pt) {
                    size_t occurrence = 0;
                    std::string lua_error;
                    if (!favourites_.resolve_description_occurrence(
                            selected_category_, selected_row_, occurrence, lua_error)) {
                        log("[error] cannot resolve selected row: " + lua_error);
                    } else if (TeleportLuaRepository(resolve_path(settings_.teleport_lua_path))
                                   .update_entry(pt->description, occurrence, pos, lua_error)) {
                        log("Teleport.lua: updated '" + pt->description + "'");
                    } else {
                        log("[error] Teleport.lua update failed: " + lua_error);
                    }
                }
            } else {
                log("[error] cannot read position: " + teleport_->last_error());
            }
        }
    }

    // 插后 表&炉石: insert the current position after the selected row in
    // both the in-memory favourites table and Teleport.lua. The selected
    // point's stored description is the Lua anchor; desc_buf_ names the new
    // row, matching the ordinary 插后 button's semantics.
    if (ImGui::Button(
            "\xe6\x8f\x92\xe5\x90\x8e \xe8\xa1\xa8&\xe7\x82\x89\xe7\x9f\xb3")) {
        if (selected_row_ < 0) {
            log("[warn] no row selected for table + Teleport.lua append");
        } else if (desc_buf_[0] == '\0') {
            log("[warn] description is empty");
        } else if (settings_.teleport_lua_path.empty()) {
            log("[warn] Teleport.lua path not configured (see Settings)");
        } else if (teleport_) {
            auto anchor = current_point();
            Position pos;
            uint32_t map_id = 0;
            uint32_t map_id_after = 0;
            size_t anchor_occurrence = 0;
            if (!anchor) {
                log("[error] selected row is no longer available");
            } else if (!favourites_.resolve_description_occurrence(
                           selected_category_, selected_row_, anchor_occurrence, error)) {
                log("[error] cannot resolve selected row: " + error);
            } else if (!teleport_->read_map_id(map_id)) {
                log("[error] cannot read mapId: " + teleport_->last_error());
            } else if (!teleport_->read_position(pos)) {
                log("[error] cannot read position: " + teleport_->last_error());
            } else if (!teleport_->read_map_id(map_id_after)) {
                log("[error] cannot verify mapId: " + teleport_->last_error());
            } else if (map_id != map_id_after) {
                log("[warn] map changed while reading position; append cancelled, try again");
            } else {
                TeleportPoint point{desc_buf_, pos};
                std::string lua_error;
                TeleportLuaRepository lua_repo(resolve_path(settings_.teleport_lua_path));
                if (!lua_repo.insert_entry_after(anchor->description, anchor_occurrence, point,
                                                  map_id, lua_error)) {
                    log("[error] Teleport.lua insert failed: " + lua_error);
                } else if (!favourites_.append_in_category(selected_category_, selected_row_, point,
                                                            error)) {
                    log("[error] table append failed after Teleport.lua insert: " + error);
                } else {
                    log("appended table + Teleport.lua: '" + point.description + "'");
                }
            }
        }
    }

    if (ImGui::Button("\xe8\xbf\xbd\xe5\x8a\xa0")) { // 追加 (add at end)
        Position pos;
        if (teleport_ && teleport_->read_position(pos) && desc_buf_[0] != '\0') {
            TeleportPoint pt{desc_buf_, pos};
            favourites_.add(pt);
            log("added " + pt.description);
        } else if (desc_buf_[0] == '\0') {
            log("[warn] description is empty");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("\xe4\xbf\x9d\xe5\xad\x98")) { // 保存 (save)
        std::string err;
        int n = favourites_.save(err);
        if (n >= 0)
            log("saved " + std::to_string(n) + " points");
        else
            log("[error] save failed: " + err);
    }
    ImGui::SameLine();
    if (ImGui::Button("\xe9\x87\x8d\xe8\xbd\xbd")) { // 重载 (reload)
        reload_favourites();
        reload_hotkeys();
        log("reloaded favourites + hotkeys");
    }
}

void App::render_teleport_button() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 10));
    // The step toggle lives on the 设置 tab now; keep its state visible here.
    const char* label = step_enabled_ ? "Teleport (step)###teleport" : "Teleport###teleport";
    if (ImGui::Button(label, ImVec2(-1, 0))) {
        auto pt = current_point();
        if (pt) do_teleport(pt->position, pt->description);
    }
    ImGui::PopStyleVar();
}

void App::render_speed_row() {
    ImGui::TextUnformatted("Global speed:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##speed", speed_buf_, sizeof(speed_buf_),
                      ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsScientific);
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        try {
            double value = std::stod(speed_buf_);
            if (features_ && features_->set_speed(value)) {
                settings_.speed_value = value;
                log("speed set to " + std::to_string(value));
            } else if (features_) {
                log("[error] speed: " + features_->last_error());
            }
        } catch (...) {
            log("[error] speed must be a number");
        }
    }
    if (!version_ || !version_->supports_speed()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsupported on this client)");
    }

    ImGui::TextUnformatted("Move speed:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##movement_speed", movement_speed_buf_, sizeof(movement_speed_buf_),
                      ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsScientific);
    ImGui::SameLine();
    const char* toggle_label = movement_speed_enabled_ ? "Disable##movement_speed"
                                                       : "Enable##movement_speed";
    if (ImGui::Button(toggle_label)) {
        if (movement_speed_enabled_) {
            if (features_ && features_->reset_movement_speed()) {
                movement_speed_enabled_ = false;
                log("movement speed restored to run=7, swim=4.7");
            } else if (features_) {
                log("[error] movement speed: " + features_->last_error());
            }
        } else {
            try {
                const double value = std::stod(movement_speed_buf_);
                if (features_ && features_->set_movement_speed(value)) {
                    settings_.movement_speed_value = value;
                    movement_speed_enabled_ = true;
                    log("movement + swim speed enabled at " + std::to_string(value));
                } else if (features_) {
                    log("[error] movement speed: " + features_->last_error());
                }
            } catch (...) {
                log("[error] movement speed must be a number");
            }
        }
    }
    if (!version_ || !version_->supports_movement_speed()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsupported on this client)");
    }
}

void App::render_import_export() {
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputText("##export_path", export_path_buf_, sizeof(export_path_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Export...")) {
        if (run_file_dialog(hwnd_, export_path_buf_, sizeof(export_path_buf_), true)) {
            std::string error;
            int n = porter_.export_to(export_path_buf_, error);
            if (n >= 0)
                log("exported " + std::to_string(n) + " points to " +
                    std::string(export_path_buf_));
            else
                log("[error] export failed: " + error);
        }
    }

    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputText("##import_path", import_path_buf_, sizeof(import_path_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
        run_file_dialog(hwnd_, import_path_buf_, sizeof(import_path_buf_), false);
    }
    if (ImGui::Button("Merge")) {
        std::string error;
        MergeReport report;
        if (porter_.import_merge(import_path_buf_, report, error)) {
            log("merged " + std::to_string(report.added) + " new, skipped " +
                std::to_string(report.skipped_duplicates) + " duplicates");
        } else {
            log("[error] " + error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Replace")) {
        std::string error;
        int n = porter_.import_replace(import_path_buf_, error);
        if (n >= 0)
            log("imported " + std::to_string(n) + " points (replaced)");
        else
            log("[error] " + error);
    }
}

void App::render_log() {
    ImGui::TextUnformatted("Log:");
    ImGui::BeginChild("##log", ImVec2(0, 120), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& line : log_.lines()) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void App::render_pid_poll_row() {
    // TeleportHackOnVanilla's 设置 tab: "WoW PID 查询间隔 [30] 秒 [应用]".
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("##pid_poll", &pid_poll_input_);
    ImGui::SameLine();
    ImGui::TextUnformatted("\xe7\xa7\x92"); // 秒
    ImGui::SameLine();
    if (ImGui::Button("\xe5\xba\x94\xe7\x94\xa8##pid_poll")) { // 应用
        const int seconds = sanitize_pid_poll_seconds(pid_poll_input_, 0);
        if (seconds == 0) {
            pid_poll_input_ = settings_.pid_poll_seconds;
            log("[warn] WoW PID poll interval must be 1-3600 seconds");
        } else {
            settings_.pid_poll_seconds = seconds;
            next_pid_poll_time_ = ImGui::GetTime() + seconds; // restart the timer
            std::string error;
            if (settings_repo_.save(settings_, error)) {
                log("WoW PID poll interval: " + std::to_string(seconds) + " s");
            } else {
                log("[error] settings save failed: " + error);
            }
        }
    }
    // 只有一个 WoW 进程时自动连接；间隔 1-3600 秒
    ImGui::TextDisabled(
        "\xe5\x8f\xaa\xe6\x9c\x89\xe4\xb8\x80\xe4\xb8\xaa WoW \xe8\xbf\x9b\xe7\xa8\x8b\xe6\x97\xb6"
        "\xe8\x87\xaa\xe5\x8a\xa8\xe8\xbf\x9e\xe6\x8e\xa5\xef\xbc\x9b\xe9\x97\xb4\xe9\x9a\x94 "
        "1-3600 \xe7\xa7\x92");
}

void App::render_paths() {
    char fav_path[512];
    std::snprintf(fav_path, sizeof(fav_path), "%s", settings_.favlist_path.c_str());
    if (ImGui::InputText("favlist.fav path", fav_path, sizeof(fav_path))) {
        settings_.favlist_path = fav_path;
    }
    char hk_path[512];
    std::snprintf(hk_path, sizeof(hk_path), "%s", settings_.hotkey_path.c_str());
    if (ImGui::InputText("hotkey.txt path", hk_path, sizeof(hk_path))) {
        settings_.hotkey_path = hk_path;
    }
    char wow_path[512];
    std::snprintf(wow_path, sizeof(wow_path), "%s", settings_.wow_exe_path.c_str());
    if (ImGui::InputText("WoW.exe path", wow_path, sizeof(wow_path))) {
        settings_.wow_exe_path = wow_path;
    }
    ImGui::TextDisabled("used by TeleportHackLauncher.exe");
    char lua_path[512];
    std::snprintf(lua_path, sizeof(lua_path), "%s", settings_.teleport_lua_path.c_str());
    if (ImGui::InputText("Teleport.lua path", lua_path, sizeof(lua_path))) {
        settings_.teleport_lua_path = lua_path;
    }
    ImGui::TextDisabled(
        "used by \xe8\xaf\xbb\xe5\x8f\x96\xe5\x88\xb0\xe7\x82\x89\xe7\x9f\xb3, leave empty to disable");

    if (ImGui::Button("Apply paths + reload")) {
        favourites_ = FavouritesService(FavlistRepository(resolve_path(settings_.favlist_path)));
        reload_favourites();
        reload_hotkeys();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save settings")) {
        std::string error;
        if (settings_repo_.save(settings_, error))
            log("settings saved");
        else
            log("[error] settings save failed: " + error);
    }
}

} // namespace th
