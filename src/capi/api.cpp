// TeleportHackDX C ABI implementation. See src/capi/api.h for the
// contract; this translation unit is the only place that mixes C-linkage
// exports with th_core internals.
#define TH_CAPI_BUILDING
#include "capi/api.h"

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "domain/models.h"
#include "domain/versions.h"
#include "memory/outofprocess_backend.h"
#include "repository/favlist_repository.h"
#include "services/favourites_service.h"
#include "services/feature_service.h"
#include "services/teleport_service.h"
#include "util/process_list.h"

namespace {

// Copies a UTF-8 std::string into a caller-provided buffer. Returns the
// number of source bytes (excluding the terminator), mirroring GetSize-then
// -GetString idiom used by every other API here.
int32_t copy_string(const std::string& source, char* out, int32_t out_size) {
    const int32_t needed = static_cast<int32_t>(source.size());
    if (out == nullptr) return needed;
    if (out_size <= 0) return needed;
    const int32_t writable = out_size - 1; // reserve NUL
    const int32_t copy = writable < needed ? writable : needed;
    if (copy > 0) std::memcpy(out, source.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return needed;
}

} // namespace

// -------------------------------------------------------------------------
// Session type. Bundles the backend + version + services identically to
// how th::App does internally in InProcess/Attach mode, minus the ImGui
// layer. Wrapped in a plain C struct so the opaque pointer can survive a
// P/Invoke round trip.
// -------------------------------------------------------------------------
struct th_session {
    std::unique_ptr<th::OutOfProcessBackend> backend;
    const th::GameVersion* version = nullptr;
    std::optional<th::TeleportService> teleport;
    std::optional<th::FeatureService> features;
    std::vector<th::TeleportPoint> favourites;
    std::string last_error;

    void rebind_services() {
        if (version && backend) {
            teleport.emplace(*backend, *version);
            features.emplace(*backend, *version);
        } else {
            teleport.reset();
            features.reset();
        }
    }
};

// -------------------------------------------------------------------------
// Versions
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_version_count(void) {
    return static_cast<int32_t>(th::all_versions().size());
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_version_name(int32_t index, char* out,
                                                                 int32_t out_size) {
    const auto& versions = th::all_versions();
    if (index < 0 || static_cast<size_t>(index) >= versions.size()) return -1;
    return copy_string(versions[static_cast<size_t>(index)].name, out, out_size);
}

// -------------------------------------------------------------------------
// Process listing
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_process_list(const char* executable, th_process_entry* out, int32_t max_entries) {
    if (executable == nullptr) return -1;
    if (max_entries < 0) return -1;
    std::vector<th::ProcessEntry> entries = th::list_processes_by_name(executable);
    if (out == nullptr) return static_cast<int32_t>(entries.size());
    const int32_t total = static_cast<int32_t>(entries.size());
    const int32_t copies = total < max_entries ? total : max_entries;
    for (int32_t i = 0; i < copies; ++i) {
        out[i].pid = static_cast<uint32_t>(entries[static_cast<size_t>(i)].pid);
        const std::string& name = entries[static_cast<size_t>(i)].exe_name;
        const size_t limit = sizeof(out[i].exe_name) - 1;
        const size_t copy = name.size() < limit ? name.size() : limit;
        std::memcpy(out[i].exe_name, name.data(), copy);
        out[i].exe_name[copy] = '\0';
    }
    return total;
}

// -------------------------------------------------------------------------
// Session lifecycle
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT th_session* TH_CAPI_CALL th_session_create(void) {
    auto* session = new (std::nothrow) th_session();
    if (!session) return nullptr;
    session->backend = std::make_unique<th::OutOfProcessBackend>();
    // Default to 3.3.5 -- same policy as th::Settings::default_version.
    session->version = th::find_version("3.3.5");
    if (!session->version) session->version = &th::all_versions().front();
    session->rebind_services();
    return session;
}

extern "C" TH_CAPI_EXPORT void TH_CAPI_CALL th_session_destroy(th_session* session) {
    delete session;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_last_error(th_session* session, char* out, int32_t out_size) {
    if (!session) return -1;
    return copy_string(session->last_error, out, out_size);
}

// -------------------------------------------------------------------------
// Attach / detach
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_attach(th_session* session,
                                                                    uint32_t pid) {
    if (!session || !session->backend) return -1;
    if (!session->backend->attach(pid)) {
        session->last_error = "attach failed";
        return -1;
    }
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_detach(th_session* session) {
    if (!session || !session->backend) return -1;
    session->backend->detach();
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_is_attached(th_session* session) {
    if (!session || !session->backend) return -1;
    return session->backend->is_attached() ? 1 : 0;
}

extern "C" TH_CAPI_EXPORT uint32_t TH_CAPI_CALL th_session_pid(th_session* session) {
    if (!session || !session->backend) return 0;
    return static_cast<uint32_t>(session->backend->pid());
}

// -------------------------------------------------------------------------
// Version selection
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_set_version(th_session* session,
                                                                        const char* name) {
    if (!session || !name) return -1;
    const th::GameVersion* v = th::find_version(name);
    if (!v) {
        session->last_error = std::string("unknown client version: ") + name;
        return -1;
    }
    session->version = v;
    session->rebind_services();
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_get_version(th_session* session,
                                                                        char* out,
                                                                        int32_t out_size) {
    if (!session) return -1;
    const std::string name = session->version ? session->version->name : std::string();
    return copy_string(name, out, out_size);
}

// -------------------------------------------------------------------------
// Position / teleport
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_read_position(th_session* session, double* x, double* y, double* z, double* r,
                          int32_t* has_r) {
    if (!session || !session->teleport) return -1;
    th::Position pos;
    if (!session->teleport->read_position(pos)) {
        session->last_error = session->teleport->last_error();
        return -1;
    }
    if (x) *x = pos.x;
    if (y) *y = pos.y;
    if (z) *z = pos.z;
    if (has_r) *has_r = pos.orientation.has_value() ? 1 : 0;
    if (r && pos.orientation.has_value()) *r = *pos.orientation;
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_teleport(th_session* session,
                                                                     double x, double y,
                                                                     double z, int32_t has_r,
                                                                     double r) {
    if (!session || !session->teleport) return -1;
    th::Position pos;
    pos.x = x;
    pos.y = y;
    pos.z = z;
    if (has_r) pos.orientation = r;
    if (!session->teleport->write_position(pos)) {
        session->last_error = session->teleport->last_error();
        return -1;
    }
    session->last_error.clear();
    return 0;
}

// -------------------------------------------------------------------------
// Speed hacks
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_set_global_speed(th_session* session, double value) {
    if (!session || !session->features) return -1;
    if (!session->features->set_speed(value)) {
        session->last_error = session->features->last_error();
        return -1;
    }
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_set_movement_speed(th_session* session, double value) {
    if (!session || !session->features) return -1;
    if (!session->features->set_movement_speed(value)) {
        session->last_error = session->features->last_error();
        return -1;
    }
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_reset_movement_speed(th_session* session) {
    if (!session || !session->features) return -1;
    if (!session->features->reset_movement_speed()) {
        session->last_error = session->features->last_error();
        return -1;
    }
    session->last_error.clear();
    return 0;
}

// -------------------------------------------------------------------------
// Feature toggles
// -------------------------------------------------------------------------

namespace {

int32_t run_toggle(th_session* session, bool (th::FeatureService::*fn)(bool&),
                    int32_t* now_on) {
    if (!session || !session->features) return -1;
    bool on = false;
    if (!(session->features.value().*fn)(on)) {
        session->last_error = session->features->last_error();
        return -1;
    }
    if (now_on) *now_on = on ? 1 : 0;
    session->last_error.clear();
    return 0;
}

} // namespace

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_toggle_anti_jump(th_session* session, int32_t* now_on) {
    return run_toggle(session, &th::FeatureService::toggle_anti_jump, now_on);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_toggle_autoloot(th_session* session, int32_t* now_on) {
    return run_toggle(session, &th::FeatureService::toggle_autoloot, now_on);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_toggle_patch_loot(th_session* session, int32_t* now_on) {
    return run_toggle(session, &th::FeatureService::toggle_patch_loot, now_on);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_session_toggle_lua_unlock(th_session* session, int32_t* now_on) {
    return run_toggle(session, &th::FeatureService::toggle_lua_unlock, now_on);
}

// -------------------------------------------------------------------------
// Favourites
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_load(th_session* session,
                                                                     const char* path) {
    if (!session || !path) return -1;
    th::FavlistRepository repo(path);
    std::vector<th::TeleportPoint> points;
    std::string error;
    if (!repo.load(points, error)) {
        session->last_error = error;
        return -1;
    }
    session->favourites = std::move(points);
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_count(th_session* session) {
    if (!session) return -1;
    return static_cast<int32_t>(session->favourites.size());
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_get_description(th_session* session, int32_t index, char* out,
                                int32_t out_size) {
    if (!session) return -1;
    if (index < 0 || static_cast<size_t>(index) >= session->favourites.size()) return -1;
    return copy_string(session->favourites[static_cast<size_t>(index)].description, out,
                        out_size);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_get_category(th_session* session, int32_t index, char* out, int32_t out_size) {
    if (!session) return -1;
    if (index < 0 || static_cast<size_t>(index) >= session->favourites.size()) return -1;
    return copy_string(session->favourites[static_cast<size_t>(index)].category(), out,
                        out_size);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_get_position(th_session* session, int32_t index, double* x, double* y,
                             double* z, double* r, int32_t* has_r) {
    if (!session) return -1;
    if (index < 0 || static_cast<size_t>(index) >= session->favourites.size()) return -1;
    const th::Position& pos = session->favourites[static_cast<size_t>(index)].position;
    if (x) *x = pos.x;
    if (y) *y = pos.y;
    if (z) *z = pos.z;
    if (has_r) *has_r = pos.orientation.has_value() ? 1 : 0;
    if (r && pos.orientation.has_value()) *r = *pos.orientation;
    return 0;
}
