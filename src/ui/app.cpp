#include "ui/app.h"

#include <Windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "imgui.h"
#include "memory/outofprocess_backend.h"
#include "repository/hotkey_config_repository.h"
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

    std::snprintf(speed_buf_, sizeof(speed_buf_), "%.6g", settings_.speed_value);
    selected_category_ = settings_.last_category.empty() ? ALL_CATEGORY : settings_.last_category;

    // porter_ was bound to favourites_ once in App::App()'s initializer
    // list; reassigning favourites_'s *contents* below keeps that
    // reference valid (the object's address doesn't change).
    favourites_ = FavouritesService(FavlistRepository(resolve_path(settings_.favlist_path)));

    hotkeys_.emplace(hwnd_);

    switch_version(settings_.default_version);
    reload_favourites();
    reload_hotkeys();
    if (mode_ == BackendMode::Attach) reload_process_list();

    log("initialized (" + (version_ ? version_->name : std::string("no version")) + ")");
}

void App::switch_version(const std::string& name) {
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

void App::attach_to_pid(unsigned long pid) {
    if (mode_ != BackendMode::Attach) return;
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

void App::do_teleport(const Position& pos) {
    if (!teleport_) return;
    if (step_enabled_) {
        StepConfig cfg;
        cfg.distance_per_step = settings_.step_distance;
        cfg.sleep_between_steps_sec = settings_.step_sleep_ms / 1000.0;
        step_session_.start(*teleport_, pos, cfg);
        log("step-teleport started");
    } else {
        if (teleport_->write_position(pos)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "teleported to (%.3f, %.3f, %.3f)", pos.x, pos.y,
                          pos.z);
            log(buf);
        } else {
            log("[error] teleport failed: " + teleport_->last_error());
        }
    }
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
    if (teleport_ && step_session_.active()) {
        step_session_.tick(*teleport_);
    }
}

bool App::on_wndproc(UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
    if (msg == WM_HOTKEY && hotkeys_) {
        Position pos;
        if (hotkeys_->resolve(static_cast<int>(wParam), pos)) {
            do_teleport(pos);
            return true;
        }
    }
    return false;
}

void App::shutdown() {
    std::string error;
    if (!settings_repo_.save(settings_, error)) {
        log("[error] settings save failed: " + error);
    }
    if (hotkeys_) hotkeys_->clear();
}

// -------------------------------------------------------------------
// Rendering

void App::render() {
    ImGui::SetNextWindowSize(ImVec2(460, 780), ImGuiCond_FirstUseEver);
    ImGui::Begin(mode_ == BackendMode::InProcess ? "TeleportHack DX"
                                                  : "TeleportHack DX (Desktop / Attach)");

    if (mode_ == BackendMode::Attach) {
        render_process_list();
        ImGui::Separator();
    }
    render_version_row();
    ImGui::Separator();
    render_category_and_search();
    render_table();
    render_desc_input();
    render_toggles_row();
    render_crud_rows();
    render_teleport_button();
    render_speed_row();
    ImGui::Separator();
    render_import_export();
    ImGui::Separator();
    render_log();

    render_settings_popup();

    ImGui::End();
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
    if (ImGui::SmallButton("Refresh")) reload_process_list();

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
    ImGui::SameLine();
    if (ImGui::Button("Settings")) show_settings_ = true;

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
            selected_category_ = ALL_CATEGORY;
            selected_row_ = -1;
        }
        for (const auto& name : favourites_.category_names()) {
            bool selected = (selected_category_ == name);
            if (ImGui::Selectable(name.c_str(), selected)) {
                selected_category_ = name;
                selected_row_ = -1;
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
    ImVec2 outer_size(0.0f, ImGui::GetTextLineHeightWithSpacing() * 14);
    if (ImGui::BeginTable("favourites", 4, flags, outer_size)) {
        ImGui::TableSetupColumn("\xe6\x8f\x8f\xe8\xbf\xb0", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed, 60.0f);
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
            if (ImGui::Selectable(pt.description.c_str(), selected, sel_flags)) {
                selected_row_ = i;
                std::snprintf(desc_buf_, sizeof(desc_buf_), "%s", pt.description.c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    do_teleport(pt.position);
                }
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", pt.position.x);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", pt.position.y);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", pt.position.z);
        }
        ImGui::EndTable();
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
    ImGui::Checkbox("step", &step_enabled_);
    ImGui::SameLine();
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
    if (ImGui::Button("Teleport", ImVec2(-1, 0))) {
        auto pt = current_point();
        if (pt) do_teleport(pt->position);
    }
    ImGui::PopStyleVar();
}

void App::render_speed_row() {
    ImGui::TextUnformatted("Speed:");
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
}

void App::render_import_export() {
    if (ImGui::TreeNodeEx("Import / Export", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        ImGui::SameLine();
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
        ImGui::TreePop();
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

void App::render_settings_popup() {
    if (!show_settings_) return;
    ImGui::Begin("Settings", &show_settings_, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::InputDouble("Step distance", &settings_.step_distance);
    int sleep_ms = settings_.step_sleep_ms;
    if (ImGui::InputInt("Step sleep (ms)", &sleep_ms)) settings_.step_sleep_ms = sleep_ms;
    ImGui::Checkbox("Fast step (skip Left/Right taps)", &settings_.fast_step);

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
    ImGui::End();
}

} // namespace th
