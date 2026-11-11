// TeleportHackDX C ABI implementation. See src/capi/api.h for the
// contract; this translation unit is the only place that mixes C-linkage
// exports with th_core internals.
#ifndef TH_CAPI_BUILDING
#define TH_CAPI_BUILDING
#endif
#include "capi/api.h"

#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "domain/app_info.h"
#include "domain/category_groups.h"
#include "domain/models.h"
#include "domain/versions.h"
#include "memory/outofprocess_backend.h"
#include "repository/favlist_repository.h"
#include "services/favourites_service.h"
#include "services/favourites_porter.h"
#include "services/feature_service.h"
#include "services/recent_teleports.h"
#include "services/teleport_service.h"
#include "repository/teleport_lua_repository.h"
#include "util/process_list.h"

namespace {

// Copies a UTF-8 std::string into a caller-provided buffer. Returns the
// number of source bytes (excluding the terminator), mirroring GetSize-then
// -GetString idiom used by every other API here.
int32_t copy_string(const std::string& source, char* out, int32_t out_size) {
    const int32_t needed = static_cast<int32_t>(source.size());
    if (out == nullptr) return out_size > 0 ? -1 : needed;
    if (out_size <= 0) return needed;
    const int32_t writable = out_size - 1; // reserve NUL
    const int32_t copy = writable < needed ? writable : needed;
    if (copy > 0) std::memcpy(out, source.data(), static_cast<size_t>(copy));
    out[copy] = '\0';
    return needed;
}

// Shared out-parameter convention: `has_*` always written (when non-NULL),
// the value itself only when present.
void fill_position(const th::Position& pos, double* x, double* y, double* z, double* r,
                   int32_t* has_r, uint32_t* map_id, int32_t* has_map) {
    if (x) *x = pos.x;
    if (y) *y = pos.y;
    if (z) *z = pos.z;
    if (has_r) *has_r = pos.orientation.has_value() ? 1 : 0;
    if (r && pos.orientation.has_value()) *r = *pos.orientation;
    if (has_map) *has_map = pos.map_id.has_value() ? 1 : 0;
    if (map_id && pos.map_id.has_value()) *map_id = *pos.map_id;
}

th::Position make_position(double x, double y, double z, int32_t has_r, double r,
                           int32_t has_map, uint32_t map_id) {
    th::Position pos;
    pos.x = x;
    pos.y = y;
    pos.z = z;
    if (has_r) pos.orientation = r;
    if (has_map) pos.map_id = map_id;
    return pos;
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
    th::RecentTeleports recent;
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

namespace {

template <typename Function>
int32_t guarded_call(th_session* session, const char* operation, Function&& function) noexcept {
    try {
        return function();
    } catch (const std::exception& ex) {
        if (session) {
            try { session->last_error = std::string(operation) + ": " + ex.what(); }
            catch (...) { }
        }
    } catch (...) {
        if (session) {
            try { session->last_error = std::string(operation) + ": unknown native exception"; }
            catch (...) { }
        }
    }
    return -1;
}

} // namespace

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
    return copies;
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
    return th_session_read_position_ex(session, x, y, z, r, has_r, nullptr, nullptr);
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
    return guarded_call(session, "load favourites", [&]() -> int32_t {
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
    });
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
    fill_position(session->favourites[static_cast<size_t>(index)].position, x, y, z, r, has_r,
                  nullptr, nullptr);
    return 0;
}

// -------------------------------------------------------------------------
// Favourites CRUD helpers. All of them route through a temporary
// FavouritesService so we can reuse the existing category-local index
// resolution + insert/append/replace/delete logic without duplicating it
// here. The service holds a *copy* of session->favourites while mutating,
// and we copy back only on success -- this way a failed operation (e.g.
// out-of-range index) leaves the session's list untouched, matching the
// C ABI's "no partial writes on -1" convention.
// -------------------------------------------------------------------------

namespace {

th::FavouritesService make_service(th_session* session) {
    // FavlistRepository's path only matters for reload/save; we never call
    // either from these helpers, so pass an empty path.
    th::FavouritesService service{th::FavlistRepository("")};
    service.replace_all(session->favourites);
    return service;
}

using MutatorFn = bool (th::FavouritesService::*)(const std::string&, int,
                                                   const th::TeleportPoint&, std::string&);

int32_t run_mutator(th_session* session, MutatorFn fn, const char* category, int32_t local_index,
                    const char* description, const th::Position& pos) {
    if (!session || !category || !description) return -1;
    return guarded_call(session, "favourites mutation", [&]() -> int32_t {
        th::TeleportPoint point{description, pos};
        auto service = make_service(session);
        std::string error;
        if (!(service.*fn)(category, local_index, point, error)) {
            session->last_error = error;
            return -1;
        }
        session->favourites = service.all_points();
        session->last_error.clear();
        return 0;
    });
}

// Map guard + write + recent-list entry, shared by th_session_teleport_ex
// and th_recent_teleport.
int32_t teleport_and_record(th_session* session, const std::string& label,
                            const th::Position& pos) {
    if (!session->teleport->can_teleport_to_map(pos) ||
        !session->teleport->write_position(pos)) {
        session->last_error = session->teleport->last_error();
        return -1;
    }
    session->recent.record(label, pos);
    session->last_error.clear();
    return 0;
}

} // namespace

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_category_count(th_session* session) {
    if (!session) return -1;
    return guarded_call(session, "enumerate categories", [&]() -> int32_t {
        auto service = make_service(session);
        return static_cast<int32_t>(service.category_names().size());
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_category_name(th_session* session, int32_t index, char* out, int32_t out_size) {
    if (!session) return -1;
    return guarded_call(session, "read category", [&]() -> int32_t {
        auto service = make_service(session);
        const auto names = service.category_names();
        if (index < 0 || static_cast<size_t>(index) >= names.size()) return -1;
        return copy_string(names[static_cast<size_t>(index)], out, out_size);
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_insert_in_category(th_session* session, const char* category, int32_t local_index,
                                   const char* description, double x, double y, double z,
                                   int32_t has_r, double r) {
    return run_mutator(session, &th::FavouritesService::insert_in_category, category, local_index,
                        description, make_position(x, y, z, has_r, r, 0, 0));
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_append_in_category(th_session* session, const char* category, int32_t local_index,
                                   const char* description, double x, double y, double z,
                                   int32_t has_r, double r) {
    return run_mutator(session, &th::FavouritesService::append_in_category, category, local_index,
                        description, make_position(x, y, z, has_r, r, 0, 0));
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_replace_in_category(th_session* session, const char* category, int32_t local_index,
                                    const char* description, double x, double y, double z,
                                    int32_t has_r, double r) {
    return run_mutator(session, &th::FavouritesService::replace_in_category, category, local_index,
                        description, make_position(x, y, z, has_r, r, 0, 0));
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_delete_in_category(th_session* session, const char* category, int32_t local_index) {
    if (!session || !category) return -1;
    return guarded_call(session, "delete favourite", [&]() -> int32_t {
        auto service = make_service(session);
        std::string error;
        th::TeleportPoint removed;
        if (!service.delete_in_category(category, local_index, removed, error)) {
            session->last_error = error;
            return -1;
        }
        session->favourites = service.all_points();
        session->last_error.clear();
        return 0;
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_save(th_session* session,
                                                                    const char* path) {
    if (!session || !path) return -1;
    return guarded_call(session, "save favourites", [&]() -> int32_t {
        th::FavlistRepository repo(path);
        std::string error;
        int written = repo.save(session->favourites, error);
        if (written < 0) {
            session->last_error = error;
            return -1;
        }
        session->last_error.clear();
        return written;
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_export(th_session* session,
                                                                       const char* path) {
    if (!session || !path) return -1;
    return guarded_call(session, "export favourites", [&]() -> int32_t {
        auto service = make_service(session);
        th::FavouritesPorter porter(service);
        std::string error;
        int written = porter.export_to(path, error);
        if (written < 0) {
            session->last_error = error;
            return -1;
        }
        session->last_error.clear();
        return written;
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_import_replace(th_session* session, const char* path) {
    if (!session || !path) return -1;
    return guarded_call(session, "import replace", [&]() -> int32_t {
        auto service = make_service(session);
        th::FavouritesPorter porter(service);
        std::string error;
        int loaded = porter.import_replace(path, error);
        if (loaded < 0) {
            session->last_error = error;
            return -1;
        }
        session->favourites = service.all_points();
        session->last_error.clear();
        return loaded;
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL
th_favourites_import_merge(th_session* session, const char* path, int32_t* added,
    int32_t* skipped_duplicates) {
    if (!session || !path) return -1;
    return guarded_call(session, "import merge", [&]() -> int32_t {
        auto service = make_service(session);
        th::FavouritesPorter porter(service);
        th::MergeReport report;
        std::string error;
        if (!porter.import_merge(path, report, error)) {
            session->last_error = error;
            return -1;
        }
        session->favourites = service.all_points();
        if (added) *added = report.added;
        if (skipped_duplicates) *skipped_duplicates = report.skipped_duplicates;
        session->last_error.clear();
        return 0;
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_teleport_lua_update_current(
    th_session* session, const char* lua_path, const char* description,
    int32_t description_occurrence) {
    if (!session || !lua_path || !description || description_occurrence < 0 ||
        !session->teleport) return -1;
    return guarded_call(session, "Teleport.lua update", [&]() -> int32_t {
        th::Position position;
        if (!session->teleport->read_position(position)) {
            session->last_error = session->teleport->last_error();
            return -1;
        }
        std::string error;
        if (!th::TeleportLuaRepository(lua_path).update_entry(
                description, static_cast<size_t>(description_occurrence), position, error)) {
            session->last_error = error;
            return -1;
        }
        session->last_error.clear();
        return 0;
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_teleport_lua_insert_current_after(
    th_session* session, const char* lua_path, const char* category,
    int32_t local_index, const char* new_description) {
    if (!session || !lua_path || !category || !new_description || !session->teleport)
        return -1;
    return guarded_call(session, "Teleport.lua + table insert", [&]() -> int32_t {
        auto service = make_service(session);
        int global_index = -1;
        size_t occurrence = 0;
        std::string error;
        if (!service.resolve_global_index(category, local_index, global_index, error) ||
            !service.resolve_description_occurrence(category, local_index, occurrence, error)) {
            session->last_error = error;
            return -1;
        }
        const th::TeleportPoint anchor = service.all_points()[static_cast<size_t>(global_index)];

        uint32_t map_before = 0, map_after = 0;
        th::Position position;
        if (!session->teleport->read_map_id(map_before) ||
            !session->teleport->read_position(position) ||
            !session->teleport->read_map_id(map_after)) {
            session->last_error = session->teleport->last_error();
            return -1;
        }
        if (map_before != map_after) {
            session->last_error = "map changed while reading position; try again";
            return -1;
        }

        th::TeleportPoint point{new_description, position};
        if (!service.append_in_category(category, local_index, point, error)) {
            session->last_error = error;
            return -1;
        }
        // Allocate the final session list before writing Lua. The subsequent
        // swap is noexcept, so an allocation failure cannot leave Lua updated
        // while the in-memory table remains stale.
        std::vector<th::TeleportPoint> committed = service.all_points();
        if (!th::TeleportLuaRepository(lua_path).insert_entry_after(
                anchor.description, occurrence, point, map_before, error)) {
            session->last_error = error;
            return -1;
        }
        session->favourites.swap(committed);
        session->last_error.clear();
        return 0;
    });
}

// -------------------------------------------------------------------------
// Map IDs + facing
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_read_position_ex(
    th_session* session, double* x, double* y, double* z, double* r, int32_t* has_r,
    uint32_t* map_id, int32_t* has_map) {
    if (!session || !session->teleport) return -1;
    th::Position pos;
    if (!session->teleport->read_position(pos)) {
        session->last_error = session->teleport->last_error();
        return -1;
    }
    fill_position(pos, x, y, z, r, has_r, map_id, has_map);
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_read_map_id(th_session* session,
                                                                        uint32_t* map_id) {
    if (!session || !session->teleport) return -1;
    uint32_t value = 0;
    if (!session->teleport->read_map_id(value)) {
        session->last_error = session->teleport->last_error();
        return -1;
    }
    if (map_id) *map_id = value;
    session->last_error.clear();
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_session_teleport_ex(
    th_session* session, const char* label, double x, double y, double z, int32_t has_r,
    double r, int32_t has_map, uint32_t map_id) {
    if (!session || !session->teleport) return -1;
    return guarded_call(session, "teleport", [&]() -> int32_t {
        return teleport_and_record(session, label ? std::string(label) : std::string(),
                                   make_position(x, y, z, has_r, r, has_map, map_id));
    });
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_get_map_id(th_session* session,
                                                                          int32_t index,
                                                                          uint32_t* map_id,
                                                                          int32_t* has_map) {
    if (!session) return -1;
    if (index < 0 || static_cast<size_t>(index) >= session->favourites.size()) return -1;
    fill_position(session->favourites[static_cast<size_t>(index)].position, nullptr, nullptr,
                  nullptr, nullptr, nullptr, map_id, has_map);
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_insert_in_category_ex(
    th_session* session, const char* category, int32_t local_index, const char* description,
    double x, double y, double z, int32_t has_r, double r, int32_t has_map, uint32_t map_id) {
    return run_mutator(session, &th::FavouritesService::insert_in_category, category, local_index,
                       description, make_position(x, y, z, has_r, r, has_map, map_id));
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_append_in_category_ex(
    th_session* session, const char* category, int32_t local_index, const char* description,
    double x, double y, double z, int32_t has_r, double r, int32_t has_map, uint32_t map_id) {
    return run_mutator(session, &th::FavouritesService::append_in_category, category, local_index,
                       description, make_position(x, y, z, has_r, r, has_map, map_id));
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_favourites_replace_in_category_ex(
    th_session* session, const char* category, int32_t local_index, const char* description,
    double x, double y, double z, int32_t has_r, double r, int32_t has_map, uint32_t map_id) {
    return run_mutator(session, &th::FavouritesService::replace_in_category, category,
                       local_index, description,
                       make_position(x, y, z, has_r, r, has_map, map_id));
}

// -------------------------------------------------------------------------
// Recent teleports
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_recent_count(th_session* session) {
    if (!session) return -1;
    return static_cast<int32_t>(session->recent.size());
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_recent_get_label(th_session* session,
                                                                     int32_t index, char* out,
                                                                     int32_t out_size) {
    if (!session || index < 0) return -1;
    th::RecentTeleport entry;
    if (!session->recent.get(static_cast<size_t>(index), entry)) return -1;
    return copy_string(entry.label, out, out_size);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_recent_get_position(
    th_session* session, int32_t index, double* x, double* y, double* z, double* r,
    int32_t* has_r, uint32_t* map_id, int32_t* has_map) {
    if (!session || index < 0) return -1;
    th::RecentTeleport entry;
    if (!session->recent.get(static_cast<size_t>(index), entry)) return -1;
    fill_position(entry.position, x, y, z, r, has_r, map_id, has_map);
    return 0;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_recent_teleport(th_session* session,
                                                                    int32_t index) {
    if (!session || !session->teleport || index < 0) return -1;
    return guarded_call(session, "recent teleport", [&]() -> int32_t {
        th::RecentTeleport entry;
        if (!session->recent.get(static_cast<size_t>(index), entry)) {
            session->last_error = "recent teleport index out of range";
            return -1;
        }
        return teleport_and_record(session, entry.label, entry.position);
    });
}

// -------------------------------------------------------------------------
// Category navigation groups + about
// -------------------------------------------------------------------------

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_category_group_count(void) {
    return th::kCategoryGroupCount;
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_category_group_name(int32_t group, char* out,
                                                                        int32_t out_size) {
    if (group < 0 || group >= th::kCategoryGroupCount) return -1;
    return copy_string(th::category_group_name(group), out, out_size);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_category_group_of(const char* category) {
    if (!category) return -1;
    return th::category_group_of(category);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_category_order(const char* category) {
    if (!category) return -1;
    return th::category_order(category);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_build_version(char* out, int32_t out_size) {
    return copy_string(th::build_version(), out, out_size);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_app_author(char* out, int32_t out_size) {
    return copy_string(th::app_author(), out, out_size);
}

extern "C" TH_CAPI_EXPORT int32_t TH_CAPI_CALL th_release_notes(char* out, int32_t out_size) {
    return copy_string(th::release_notes(), out, out_size);
}
