// Orientation/facing compatibility tests -- runs for real on the Windows
// CI runner via CTest (see CMakeLists.txt's orientation_tests target and
// .github/workflows/build.yml's `ctest` step). Deliberately a plain
// assert-based executable (no test framework dependency) mirroring the
// throwaway g++ smoke-test harnesses used during development.
//
// Covers: old 4-field favlist.fav lines, new 5-field lines (empty / zero /
// non-zero orientation as *distinct* cases), legacy multi-line records
// (4-line and 5-line), save() round-trip normalising to canonical 5-field
// lines, merge-dedup distinguishing missing/zero/non-zero orientation, and
// 3.3.5's pos_r read/write-only-when-present behaviour via a FakeBackend
// (also confirms 1.12.x never touches pos_r since those profiles have
// pos_r == 0).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/models.h"
#include "domain/versions.h"
#include "memory/memory_backend.h"
#include "repository/favlist_repository.h"
#include "services/favourites_porter.h"
#include "services/favourites_service.h"
#include "services/teleport_service.h"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near_eq(double a, double b) { return std::fabs(a - b) < 1e-6; }

// Minimal in-memory MemoryBackend stub, keyed by address, so
// TeleportService's pointer-chain resolution and pos_r handling can be
// exercised without a real WoW process.
class FakeBackend final : public th::MemoryBackend {
public:
    bool is_attached() const override { return true; }

    bool read_float(uintptr_t address, float& out) override {
        auto it = floats.find(address);
        if (it == floats.end()) return false;
        out = it->second;
        return true;
    }
    bool read_uint32(uintptr_t, uint32_t&) override { return false; }
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
    std::unordered_map<uintptr_t, uintptr_t> pointers;
    std::unordered_map<uintptr_t, int> write_count;
};

void test_favlist_formats(const std::filesystem::path& dir) {
    auto input = dir / "input.fav";
    {
        std::ofstream out(input, std::ios::binary);
        out << "old-no-r#1#2#3\n";
        out << "new-empty-r#4#5#6#\n";
        out << "new-zero-r#7#8#9#0\n";
        out << "new-r#10#11#12#1.857\n";
    }

    std::vector<th::TeleportPoint> points;
    std::string error;
    th::FavlistRepository repo(input.string());
    check(repo.load(points, error), "favlist with old/new records loads");
    check(points.size() == 4, "all four old/new records are retained");
    check(!points[0].position.orientation.has_value(), "old 4-field record has no orientation");
    check(!points[1].position.orientation.has_value(), "empty fifth field has no orientation");
    check(points[2].position.orientation.has_value() &&
              near_eq(*points[2].position.orientation, 0.0),
          "explicit zero orientation is retained (not treated as empty)");
    check(points[3].position.orientation.has_value() &&
              near_eq(*points[3].position.orientation, 1.857),
          "non-zero orientation is parsed");

    // Legacy multi-line: 4-line record (no orientation line) and 5-line
    // record (with orientation line, including an empty one).
    auto legacy = dir / "legacy.fav";
    {
        std::ofstream out(legacy, std::ios::binary);
        out << "legacy-a\n1\n2\n3\n";
        out << "legacy-with-r\n4\n5\n6\n2.5\n";
        out << "legacy-empty-r\n7\n8\n9\n\n";
    }
    std::vector<th::TeleportPoint> legacy_points;
    check(th::FavlistRepository(legacy.string()).load(legacy_points, error),
          "legacy multi-line favlist loads");
    check(legacy_points.size() == 3, "all three legacy records are retained");
    check(!legacy_points[0].position.orientation.has_value(), "legacy 4-line record has no r");
    check(legacy_points[1].position.orientation.has_value() &&
              near_eq(*legacy_points[1].position.orientation, 2.5),
          "legacy 5-line record's r line is parsed");
    check(!legacy_points[2].position.orientation.has_value(),
          "legacy 5-line record with an empty r line has no orientation");

    auto output = dir / "output.fav";
    std::string save_error;
    check(th::FavlistRepository(output.string()).save(points, save_error) == 4,
          "all points save successfully");
    std::ifstream saved_file(output, std::ios::binary);
    std::string saved((std::istreambuf_iterator<char>(saved_file)),
                      std::istreambuf_iterator<char>());
    check(saved.find("old-no-r#1#2#3#\n") != std::string::npos,
          "old record normalises to canonical 5-field line with empty r");
    check(saved.find("new-zero-r#7#8#9#0\n") != std::string::npos,
          "explicit zero orientation round-trips");
    check(saved.find("new-r#10#11#12#1.857\n") != std::string::npos,
          "non-zero orientation round-trips");

    // Malformed orientation field: skip the record, don't crash.
    auto bad = dir / "bad.fav";
    {
        std::ofstream out(bad, std::ios::binary);
        out << "good-one#1#2#3#\n";
        out << "bad-orientation#1#2#3#not-a-number\n";
        out << "good-two#4#5#6#7\n";
    }
    std::vector<th::TeleportPoint> bad_points;
    check(th::FavlistRepository(bad.string()).load(bad_points, error),
          "favlist with a malformed orientation field still loads (doesn't error out)");
    check(bad_points.size() == 2, "malformed-orientation record is skipped, others kept");
}

void test_merge_signature_includes_orientation(const std::filesystem::path& dir) {
    auto path = dir / "merge-base.fav";
    th::FavouritesService favourites{th::FavlistRepository(path.string())};
    favourites.add(th::TeleportPoint{"same", th::Position{1, 2, 3, std::nullopt}});
    th::FavouritesPorter porter(favourites);

    std::vector<th::TeleportPoint> incoming = {
        {"same", th::Position{1, 2, 3, std::nullopt}},
        {"same", th::Position{1, 2, 3, 0.0}},
        {"same", th::Position{1, 2, 3, 1.857}},
    };
    th::MergeReport report = porter.merge_points(incoming);
    check(report.skipped_duplicates == 1, "exact missing-orientation duplicate is skipped");
    check(report.added == 2, "zero and non-zero orientations are distinct merge records");
}

void test_335_orientation_memory() {
    const th::GameVersion* version = th::find_version("3.3.5");
    check(version != nullptr, "3.3.5 profile exists");
    if (!version) return;

    FakeBackend backend;
    constexpr uintptr_t pb1 = 0x1000, pb2 = 0x2000, base = 0x3000;
    backend.pointers[version->static_player] = pb1;
    backend.pointers[pb1 + version->pb_pointer1] = pb2;
    backend.pointers[pb2 + version->pb_pointer2] = base;
    backend.floats[base + version->pos_x] = 10.0f;
    backend.floats[base + version->pos_y] = 20.0f;
    backend.floats[base + version->pos_z] = 30.0f;
    backend.floats[base + version->pos_r] = 1.857f;

    th::TeleportService service(backend, *version);
    th::Position current;
    check(service.read_position(current), "3.3.5 position+orientation reads");
    check(current.orientation.has_value() && near_eq(*current.orientation, 1.857),
          "read_position reads pos_r on 3.3.5");

    check(service.write_position(th::Position{100, 200, 300, 4.559}),
          "position with orientation writes");
    check(near_eq(backend.floats[base + version->pos_x], 200), "x/y axis swap retained on write");
    check(near_eq(backend.floats[base + version->pos_y], 100), "y/x axis swap retained on write");
    check(near_eq(backend.floats[base + version->pos_r], 4.559), "pos_r is written when present");

    int orientation_writes_before = backend.write_count[base + version->pos_r];
    check(service.write_position(th::Position{101, 201, 301, std::nullopt}),
          "position without orientation still writes coordinates");
    check(backend.write_count[base + version->pos_r] == orientation_writes_before,
          "missing orientation does not write pos_r");
    check(near_eq(backend.floats[base + version->pos_r], 4.559),
          "missing orientation preserves the previously-written facing");
}

void test_112x_never_touches_pos_r() {
    for (const char* name : {"1.12.1", "1.12.3"}) {
        const th::GameVersion* version = th::find_version(name);
        check(version != nullptr, "1.12.x profile exists");
        if (!version) continue;
        check(version->pos_r == 0, (std::string(name) + " has pos_r == 0 (no orientation offset)")
                                        .c_str());

        FakeBackend backend;
        backend.floats[version->curr_pos_x] = 1.0f;
        backend.floats[version->curr_pos_y] = 2.0f;
        backend.floats[version->curr_pos_z] = 3.0f;
        th::TeleportService service(backend, *version);
        th::Position pos;
        check(service.read_position(pos), (std::string(name) + " position reads").c_str());
        check(!pos.orientation.has_value(),
              (std::string(name) + " read_position never sets an orientation").c_str());
    }
}

} // namespace

int main() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "teleporthackdx-orientation-tests";
    std::filesystem::create_directories(dir);

    test_favlist_formats(dir);
    test_merge_signature_includes_orientation(dir);
    test_335_orientation_memory();
    test_112x_never_touches_pos_r();

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "orientation compatibility tests passed\n";
    return 0;
}
