// TeleportHackOnVanilla sync tests (d462c4c "支持收藏坐标的地图与朝向字段",
// 309516d "适配 1.12.1 地图和朝向", d25a20f/f34e471 recent teleports +
// category navigation). Plain assert-style executable like the other
// tests so it runs unchanged under CTest on the Windows CI runner.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/app_info.h"
#include "domain/category_groups.h"
#include "domain/models.h"
#include "domain/versions.h"
#include "memory/memory_backend.h"
#include "repository/favlist_repository.h"
#include "repository/settings_repository.h"
#include "services/favourites_porter.h"
#include "services/favourites_service.h"
#include "services/recent_teleports.h"
#include "services/teleport_service.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near_eq(double a, double b) { return std::fabs(a - b) < 1e-4; }

class FakeBackend final : public th::MemoryBackend {
public:
    bool is_attached() const override { return true; }

    bool read_float(uintptr_t address, float& out) override {
        auto it = floats.find(address);
        if (it == floats.end()) return false;
        out = it->second;
        return true;
    }
    bool read_uint32(uintptr_t address, uint32_t& out) override {
        auto it = dwords.find(address);
        if (it == dwords.end()) return false;
        out = it->second;
        return true;
    }
    bool read_byte(uintptr_t, uint8_t&) override { return false; }
    bool read_pointer(uintptr_t address, uintptr_t& out) override {
        auto it = pointers.find(address);
        if (it == pointers.end()) return false;
        out = it->second;
        return true;
    }
    bool read_bytes(uintptr_t, void*, size_t) override { return false; }

    bool write_float(uintptr_t address, float value) override {
        floats[address] = value;
        ++write_count[address];
        return true;
    }
    bool write_byte(uintptr_t, uint8_t) override { return true; }

    std::unordered_map<uintptr_t, float> floats;
    std::unordered_map<uintptr_t, uint32_t> dwords;
    std::unordered_map<uintptr_t, uintptr_t> pointers;
    std::unordered_map<uintptr_t, int> write_count;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Builds the three 1.12.x destination chains inside `backend` and returns
// the final X/Y/Z addresses (facing lives at Z + dst_r_offset).
struct VanillaChains {
    uintptr_t ax = 0, ay = 0, az = 0;
};

VanillaChains install_vanilla_chains(FakeBackend& backend, const th::GameVersion& v) {
    VanillaChains result;
    uintptr_t next_node = 0x10000;
    auto build = [&](const std::vector<uintptr_t>& offsets) {
        uintptr_t address = v.static_player;
        for (uintptr_t offset : offsets) {
            auto it = backend.pointers.find(address);
            uintptr_t node;
            if (it != backend.pointers.end()) {
                node = it->second; // shared prefix (0x88, 0x28) reuses nodes
            } else {
                node = next_node;
                next_node += 0x10000;
                backend.pointers[address] = node;
            }
            address = node + offset;
        }
        return address;
    };
    result.ax = build(v.dst_x_offsets);
    result.ay = build(v.dst_y_offsets);
    result.az = build(v.dst_z_offsets);
    return result;
}

// --------------------------------------------------------------------------

void test_favlist_map_field(const std::filesystem::path& dir) {
    const auto input = dir / "map.fav";
    {
        std::ofstream out(input, std::ios::binary);
        // Python port's fixture (win/tests/unit/test_favlist_repository.py)
        out << "a#1#2#3#0#0\n";
        out << "b#4#5#6#1.5#230\n";
        out << "c#7#8#9\n";
        out << "d#1#2#3##0\n";
        // AutoIt's Save() pads missing fields: 6 fields with empty r/map.
        out << "e#1#2#3##\n";
        out << "f#1#2#3#\n";
        out << "bad-negative#1#2#3##-1\n";
        out << "bad-fraction#1#2#3##1.5\n";
        out << "bad-text#1#2#3#0#abc\n";
        out << "bad-overflow#1#2#3#0#4294967296\n";
        out << "g#1#2#3#0.25#4294967295\n";
    }
    std::vector<th::TeleportPoint> points;
    std::string error;
    th::FavlistRepository repo(input.string());
    check(repo.load(points, error), "6-field favlist loads");
    check(points.size() == 7, "malformed map ids are skipped, valid rows kept (got " +
                                  std::to_string(points.size()) + ")");
    if (points.size() != 7) return;

    check(points[0].position.orientation == 0.0 && points[0].position.map_id == 0u,
          "explicit facing 0 + map 0 are kept (not treated as empty)");
    check(points[1].position.orientation.has_value() &&
              near_eq(*points[1].position.orientation, 1.5) && points[1].position.map_id == 230u,
          "facing + map parsed");
    check(!points[2].position.orientation && !points[2].position.map_id,
          "4-field row has neither facing nor map");
    check(!points[3].position.orientation && points[3].position.map_id == 0u,
          "empty facing with map 0");
    check(!points[4].position.orientation && !points[4].position.map_id,
          "AutoIt padded empty r/map fields are unset");
    check(!points[5].position.orientation && !points[5].position.map_id,
          "5-field row with empty r");
    check(points[6].position.map_id == 4294967295u, "uint32 max map id accepted");

    const auto output = dir / "map-out.fav";
    check(th::FavlistRepository(output.string()).save(points, error) == 7, "map rows save");
    const std::string saved = read_file(output);
    check(saved ==
              "a#1#2#3#0#0\n"
              "b#4#5#6#1.5#230\n"
              "c#7#8#9#\n"
              "d#1#2#3##0\n"
              "e#1#2#3#\n"
              "f#1#2#3#\n"
              "g#1#2#3#0.25#4294967295\n",
          "save keeps 5-field lines without map and appends the map field otherwise:\n" +
              saved);

    std::vector<th::TeleportPoint> reloaded;
    check(th::FavlistRepository(output.string()).load(reloaded, error), "saved file reloads");
    bool same = reloaded.size() == points.size();
    for (size_t i = 0; same && i < points.size(); ++i) {
        same = reloaded[i].description == points[i].description &&
               reloaded[i].position.orientation == points[i].position.orientation &&
               reloaded[i].position.map_id == points[i].position.map_id;
    }
    check(same, "facing/map survive a save/load round trip");
}

void test_merge_signature_includes_map(const std::filesystem::path& dir) {
    th::FavouritesService favourites{th::FavlistRepository((dir / "merge.fav").string())};
    th::Position base{1, 2, 3, std::nullopt};
    favourites.add(th::TeleportPoint{"same", base});
    th::FavouritesPorter porter(favourites);

    th::Position map0 = base;
    map0.map_id = 0;
    th::Position map1 = base;
    map1.map_id = 1;
    th::MergeReport report = porter.merge_points({{"same", base}, {"same", map0}, {"same", map1}});
    check(report.skipped_duplicates == 1, "identical row without map is a duplicate");
    check(report.added == 2, "map 0 and map 1 are distinct from 'no map'");
}

void test_vanilla_read_order_facing_and_map() {
    for (const char* name : {"1.12.1", "1.12.3"}) {
        const th::GameVersion* v = th::find_version(name);
        check(v != nullptr, std::string(name) + " profile exists");
        if (!v) continue;
        check(v->map_id != 0, std::string(name) + " has a MapID address");
        check(v->dst_r_offset == 4, std::string(name) + " facing is Z + 4");
        check(v->supports_orientation() && v->supports_map_id(),
              std::string(name) + " reports facing + map support");

        // AutoIt ReadPosition(): both clients expose the UI x at the lower
        // of the two curr_pos addresses (1.12.1 swaps CurrX/CurrY, 1.12.3
        // doesn't, because their constants are declared the other way round).
        const uintptr_t lower = v->curr_pos_x < v->curr_pos_y ? v->curr_pos_x : v->curr_pos_y;
        const uintptr_t upper = v->curr_pos_x < v->curr_pos_y ? v->curr_pos_y : v->curr_pos_x;

        FakeBackend backend;
        backend.floats[lower] = 100.0f;
        backend.floats[upper] = 200.0f;
        backend.floats[v->curr_pos_z] = 300.0f;
        th::TeleportService service(backend, *v);

        th::Position pos;
        check(service.read_position(pos), std::string(name) + " position reads without chains");
        check(near_eq(pos.x, 100) && near_eq(pos.y, 200) && near_eq(pos.z, 300),
              std::string(name) + " reads the lower curr_pos address as x");
        check(!pos.orientation && !pos.map_id,
              std::string(name) + " unloaded movement chain / map leave fields unset");

        VanillaChains chains = install_vanilla_chains(backend, *v);
        backend.floats[chains.az + v->dst_r_offset] = 1.25f;
        backend.dwords[v->map_id] = 230;
        check(service.read_position(pos), std::string(name) + " position + facing + map read");
        check(pos.orientation.has_value() && near_eq(*pos.orientation, 1.25),
              std::string(name) + " facing read from Z chain + 4");
        check(pos.map_id == 230u, std::string(name) + " map id read");

        th::Position target{10, 20, 30, 2.25};
        check(service.write_position(target), std::string(name) + " write with facing");
        check(near_eq(backend.floats[chains.ax], 20) && near_eq(backend.floats[chains.ay], 10) &&
                  near_eq(backend.floats[chains.az], 30),
              std::string(name) + " destination chains receive (y, x, z) like WritePosition($y,$x,$z)");
        check(near_eq(backend.floats[chains.az + v->dst_r_offset], 2.25),
              std::string(name) + " facing written to Z + 4");

        const int facing_writes = backend.write_count[chains.az + v->dst_r_offset];
        check(service.write_position(th::Position{11, 21, 31, std::nullopt}),
              std::string(name) + " write without facing");
        check(backend.write_count[chains.az + v->dst_r_offset] == facing_writes &&
                  near_eq(backend.floats[chains.az + v->dst_r_offset], 2.25),
              std::string(name) + " empty facing keeps the current orientation");

        // Null pointer in the chain = player not loaded: write fails cleanly.
        backend.pointers[v->static_player] = 0;
        check(!service.write_position(target), std::string(name) + " null chain pointer fails");
    }
}

void test_335_map_read() {
    const th::GameVersion* v = th::find_version("3.3.5");
    check(v != nullptr, "3.3.5 profile exists");
    if (!v) return;
    FakeBackend backend;
    backend.pointers[v->static_player] = 0x1000;
    backend.pointers[0x1000 + v->pb_pointer1] = 0x2000;
    backend.pointers[0x2000 + v->pb_pointer2] = 0x3000;
    backend.floats[0x3000 + v->pos_x] = 1.0f;
    backend.floats[0x3000 + v->pos_y] = 2.0f;
    backend.floats[0x3000 + v->pos_z] = 3.0f;
    backend.floats[0x3000 + v->pos_r] = 0.5f;
    backend.dwords[v->map_id] = 571;
    th::TeleportService service(backend, *v);
    th::Position pos;
    check(service.read_position(pos), "3.3.5 position reads");
    check(near_eq(pos.x, 1) && near_eq(pos.y, 2) && pos.map_id == 571u &&
              pos.orientation.has_value(),
          "3.3.5 read fills facing and map");

    backend.pointers[0x2000 + v->pb_pointer2] = 0;
    check(!service.read_position(pos), "3.3.5 null player base is an error");
}

void test_cross_map_guard() {
    const th::GameVersion* v = th::find_version("1.12.1");
    if (!v) return;
    FakeBackend backend;
    th::TeleportService service(backend, *v);

    th::Position no_map{1, 2, 3, std::nullopt};
    check(service.can_teleport_to_map(no_map), "points without a map always pass");

    th::Position map230 = no_map;
    map230.map_id = 230;
    check(!service.can_teleport_to_map(map230), "unreadable current map cancels");
    check(service.last_error().find("cannot verify") != std::string::npos,
          "unverifiable map explains itself: " + service.last_error());

    backend.dwords[v->map_id] = 0;
    check(!service.can_teleport_to_map(map230), "map 0 -> 230 is cancelled");
    check(service.last_error().find("cross-map") != std::string::npos,
          "cross-map error message: " + service.last_error());
    th::Position map0 = no_map;
    map0.map_id = 0;
    check(service.can_teleport_to_map(map0), "map 0 -> 0 passes (0 is a real map)");

    backend.dwords[v->map_id] = 230;
    check(service.can_teleport_to_map(map230), "same map passes");

    th::GameVersion no_map_profile = *v;
    no_map_profile.map_id = 0;
    th::TeleportService blind(backend, no_map_profile);
    check(!blind.can_teleport_to_map(map230), "client without MapID can't verify -> cancel");

    // Both step implementations refuse to start on a map mismatch.
    backend.dwords[v->map_id] = 1;
    VanillaChains chains = install_vanilla_chains(backend, *v);
    backend.floats[v->curr_pos_x] = 0.0f;
    backend.floats[v->curr_pos_y] = 0.0f;
    backend.floats[v->curr_pos_z] = 0.0f;
    int steps = -1;
    th::StepConfig cfg;
    cfg.sleep_between_steps_sec = 0;
    check(!service.teleport_step(map230, cfg, steps) && steps == 0,
          "blocking step teleport refuses a cross-map target");
    check(backend.write_count.empty(), "no coordinates written for a cross-map target");
    th::TeleportStepSession session;
    check(!session.start(service, map230, cfg) && !session.active(),
          "non-blocking step session refuses a cross-map target");

    backend.dwords[v->map_id] = 230;
    check(service.teleport_step(map230, cfg, steps) && steps >= 1,
          "same-map step teleport runs");
    check(near_eq(backend.floats[chains.az], 3), "step teleport reaches the target z");
}

void test_recent_teleports() {
    th::RecentTeleports recent;
    auto at = [](double x, std::optional<uint32_t> map = std::nullopt,
                 std::optional<double> r = std::nullopt) {
        th::Position p{x, x + 1, x + 2, r};
        p.map_id = map;
        return p;
    };
    recent.record("a", at(1));
    recent.record("b", at(2));
    recent.record("", at(3));
    check(recent.size() == 3, "three distinct teleports recorded");
    check(recent.entries()[0].label == "\xe5\x9d\x90\xe6\xa0\x87\xe7\x82\xb9",
          "empty label becomes the default label (zuobiaodian)");
    check(recent.entries()[2].label == "a", "oldest entry is last");

    recent.record("a-again", at(1, std::nullopt, 0.5));
    check(recent.size() == 3, "same place (facing ignored) is not duplicated");
    check(recent.entries()[0].label == "a-again" &&
              recent.entries()[0].position.orientation == 0.5,
          "re-teleport moves the entry to the top with the new label/facing");

    recent.record("map-1", at(1, 1u));
    check(recent.size() == 4, "same coordinates on another map are a new entry");

    for (int i = 0; i < 30; ++i) recent.record("n" + std::to_string(i), at(100 + i));
    check(recent.size() == th::RecentTeleports::kLimit, "list is capped at 20");
    check(recent.entries()[0].label == "n29", "newest first after overflow");
    th::RecentTeleport out;
    check(recent.get(19, out) && !recent.get(20, out), "get() bounds");
}

void test_category_groups() {
    const std::string elwynn = "\xe8\x89\xbe\xe5\xb0\x94\xe6\x96\x87\xe6\xa3\xae\xe6\x9e\x97";
    const std::string ashenvale = "\xe7\x81\xb0\xe8\xb0\xb7";
    const std::string nagrand = "\xe7\xba\xb3\xe6\xa0\xbc\xe5\x85\xb0";
    const std::string icecrown = "\xe5\x86\xb0\xe5\x86\xa0\xe5\x86\xb0\xe5\xb7\x9d";
    const std::string capital = "\xe4\xb8\xbb\xe5\x9f\x8e";                       // 主城
    const std::string dolanaar = "\xe5\xa4\x9a\xe5\x85\xb0\xe7\xba\xb3\xe5\xb0\x94"; // 多兰纳尔
    const std::string dungeon60 = "\xe5\x89\xaf\xe6\x9c\xac\x36\x30";              // 副本60

    check(th::category_group_of(elwynn) == 1, "Elwynn Forest -> Eastern Kingdoms");
    check(th::category_group_of(ashenvale) == 2, "Ashenvale -> Kalimdor");
    check(th::category_group_of(nagrand) == 3, "Nagrand -> The Burning Crusade");
    check(th::category_group_of(icecrown) == 4, "Icecrown -> Wrath of the Lich King");
    check(th::category_group_of(capital) == 0, "capital cities -> misc");
    check(th::category_group_of(dolanaar) == 0, "Dolanaar is no longer listed -> misc");
    check(th::category_group_name(1) == "\xe4\xb8\x9c\xe9\x83\xa8\xe7\x8e\x8b\xe5\x9b\xbd",
          "group 1 name");
    check(th::category_group_name(5).empty() && th::category_group_name(-1).empty(),
          "out-of-range group names are empty");

    auto groups = th::group_categories({capital, elwynn, nagrand, dungeon60, th::ALL_CATEGORY});
    check(groups.size() == static_cast<size_t>(th::kCategoryGroupCount), "five groups");
    if (groups.size() != 5) return;
    check(groups[0].name == "\xe6\x9d\x82\xe9\xa1\xb9", "group 0 is misc");
    check((groups[0].categories == std::vector<std::string>{th::ALL_CATEGORY, capital, dungeon60}),
          "misc starts with ALL_CATEGORY and keeps first-appearance order");
    check((groups[1].categories == std::vector<std::string>{elwynn}), "Eastern Kingdoms bucket");
    check(groups[2].categories.empty() && groups[4].categories.empty(),
          "empty groups are kept (shown as headers only)");
    check((groups[3].categories == std::vector<std::string>{nagrand}), "The Burning Crusade bucket");
}

void test_app_info_and_settings_helpers() {
    check(!th::build_version().empty(), "build version is never empty");
    check(th::app_author() == "shan.he", "author matches about.au3");
    check(th::release_notes().find("0.1.3") != std::string::npos &&
              th::release_notes().find("mapid") != std::string::npos,
          "release notes mention the map sync");

    check(th::sanitize_pid_poll_seconds(0) == 30, "0 s falls back to 30");
    check(th::sanitize_pid_poll_seconds(3601) == 30, "> 3600 s falls back to 30");
    check(th::sanitize_pid_poll_seconds(1) == 1 && th::sanitize_pid_poll_seconds(3600) == 3600,
          "1..3600 accepted");
}

} // namespace

int main() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "teleporthackdx-sync-tests";
    std::filesystem::create_directories(dir);

    test_favlist_map_field(dir);
    test_merge_signature_includes_map(dir);
    test_vanilla_read_order_facing_and_map();
    test_335_map_read();
    test_cross_map_guard();
    test_recent_teleports();
    test_category_groups();
    test_app_info_and_settings_helpers();

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "vanilla sync tests passed\n";
    return 0;
}
