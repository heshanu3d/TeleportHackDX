// End-to-end C ABI smoke test.
//
// Links src/capi/api.cpp directly (no dynamic loading) so we can assert
// behaviour of every MVP entry point without needing the SHARED library
// to already be findable on the OS loader path. We DO NOT define
// TH_CAPI_BUILDING here: the header should compile cleanly in the
// "declare-only" mode from an external caller's point of view. On
// non-Windows platforms the dllimport annotation is a no-op (see
// TH_CAPI_EXPORT in api.h), so linking against the same api.cpp still
// resolves every symbol.
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "capi/api.h"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string read_string(th_session* session,
                          int32_t (*getter)(th_session*, char*, int32_t)) {
    // Two-call idiom: first sizing, then read.
    const int32_t needed = getter(session, nullptr, 0);
    if (needed <= 0) return {};
    std::string buffer(static_cast<size_t>(needed) + 1, '\0');
    getter(session, buffer.data(), static_cast<int32_t>(buffer.size()));
    buffer.resize(static_cast<size_t>(needed));
    return buffer;
}

std::string read_indexed(th_session* session, int32_t index,
                           int32_t (*getter)(th_session*, int32_t, char*, int32_t)) {
    const int32_t needed = getter(session, index, nullptr, 0);
    if (needed < 0) return {};
    std::string buffer(static_cast<size_t>(needed) + 1, '\0');
    getter(session, index, buffer.data(), static_cast<int32_t>(buffer.size()));
    buffer.resize(static_cast<size_t>(needed));
    return buffer;
}

void test_version_enumeration() {
    const int32_t count = th_version_count();
    check(count >= 3, "at least three client profiles are enumerated");

    // Every valid index must copy a non-empty UTF-8 name.
    bool saw_335 = false;
    for (int32_t i = 0; i < count; ++i) {
        char buf[64] = {0};
        const int32_t needed = th_version_name(i, buf, sizeof(buf));
        check(needed > 0, "th_version_name returns a positive byte count");
        check(std::strlen(buf) == static_cast<size_t>(needed),
              "th_version_name NUL-terminates output");
        if (std::strcmp(buf, "3.3.5") == 0) saw_335 = true;
    }
    check(saw_335, "3.3.5 is one of the enumerated versions");

    // Out-of-range indexes and NULL-with-nonzero-size are hard errors.
    char scratch[8] = {0};
    check(th_version_name(-1, scratch, sizeof(scratch)) == -1,
          "negative version index rejected");
    check(th_version_name(count, scratch, sizeof(scratch)) == -1,
          "past-the-end version index rejected");
    check(th_version_name(0, nullptr, 8) == -1,
          "NULL output with positive capacity is rejected");
}

void test_buffer_truncation() {
    char small[4] = {'X', 'X', 'X', 'X'};
    // "3.3.5" is 5 bytes; small can hold at most 3 + NUL.
    const int32_t needed = th_version_name(2, small, sizeof(small));
    check(needed == 5, "truncated call still reports full size");
    check(small[sizeof(small) - 1] == '\0', "truncated buffer is NUL terminated");
    check(std::strncmp(small, "3.3", 3) == 0, "truncated prefix preserved");

    // Zero-size buffer must not touch memory but should still report size.
    char sentinel = 0x55;
    check(th_version_name(2, &sentinel, 0) == 5,
          "zero-capacity size probe still returns needed bytes");
    check(sentinel == 0x55, "zero-capacity size probe leaves buffer unmodified");
}

void test_session_lifecycle_defaults() {
    th_session* session = th_session_create();
    check(session != nullptr, "session creation succeeds");
    if (!session) return;

    check(th_session_is_attached(session) == 0, "new session is not attached");
    check(th_session_pid(session) == 0, "new session reports pid 0");

    const std::string version = read_string(session, th_session_get_version);
    check(version == "3.3.5", "new session defaults to 3.3.5 (matches Settings default)");

    // Setting to a known version succeeds; unknown returns -1 and captures error.
    check(th_session_set_version(session, "1.12.1") == 0, "known version accepted");
    const std::string v2 = read_string(session, th_session_get_version);
    check(v2 == "1.12.1", "version selection round-trips through get_version");

    check(th_session_set_version(session, "nonexistent") == -1,
          "unknown version rejected");
    const std::string err = read_string(session, th_session_last_error);
    check(err.find("unknown client version") != std::string::npos,
          "unknown version populates last_error with a useful message");

    // Successful call clears last_error again.
    check(th_session_set_version(session, "3.3.5") == 0, "resetting to 3.3.5 succeeds");
    const std::string err_after = read_string(session, th_session_last_error);
    check(err_after.empty(), "successful call clears last_error");

    th_session_destroy(session);
    // Destroying twice / destroying NULL is safe (documented no-op).
    th_session_destroy(nullptr);
}

void test_attached_operations_fail_gracefully() {
    th_session* session = th_session_create();
    if (!session) return;

    // Not attached, so every service call should fail rather than crash.
    double dummy = 0;
    check(th_session_read_position(session, &dummy, &dummy, &dummy, &dummy, nullptr) == -1,
          "read_position without attach fails");
    check(th_session_teleport(session, 0, 0, 0, 0, 0) == -1,
          "teleport without attach fails");
    check(th_session_set_global_speed(session, 7.0) == -1,
          "global speed without attach fails");
    check(th_session_set_movement_speed(session, 25.0) == -1,
          "movement speed without attach fails");
    check(th_session_reset_movement_speed(session) == -1,
          "reset movement speed without attach fails");

    int32_t now_on = -1;
    check(th_session_toggle_anti_jump(session, &now_on) == -1,
          "anti_jump toggle without attach fails");
    check(th_session_toggle_autoloot(session, &now_on) == -1,
          "autoloot toggle without attach fails");
    check(th_session_toggle_patch_loot(session, &now_on) == -1,
          "patch_loot toggle without attach fails");
    check(th_session_toggle_lua_unlock(session, &now_on) == -1,
          "lua_unlock toggle without attach fails");

    th_session_destroy(session);
}

void test_favourites_from_file(const std::filesystem::path& dir) {
    // Real UTF-8 favlist covering: single "描述-地名" (categorises under 描述),
    // an entry without dash (falls back to OTHER_CATEGORY), and an entry with
    // explicit orientation. Uses the canonical 5-field format so we exercise
    // the same parser production the app writes.
    const std::filesystem::path path = dir / "capi_smoke.fav";
    {
        std::ofstream out(path, std::ios::binary);
        out << "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-"
            << "\xe5\x85\xa5\xe5\x8f\xa3#1#2#3#\n"; // 斯坦索姆-入口, no orientation
        out << "\xe6\x97\xa0\xe5\x88\x86\xe7\xb1\xbb#4#5#6#\n"; // 无分类
        out << "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-"
            << "\xe5\x87\xba\xe5\x8f\xa3#7#8#9#1.857\n"; // 斯坦索姆-出口, r=1.857
    }

    th_session* session = th_session_create();
    if (!session) return;

    check(th_favourites_load(session, path.string().c_str()) == 0, "favlist loads");
    check(th_favourites_count(session) == 3, "all three points loaded");

    // Index 0 -> 斯坦索姆 category, no orientation.
    const std::string d0 =
        read_indexed(session, 0, th_favourites_get_description);
    const std::string c0 = read_indexed(session, 0, th_favourites_get_category);
    check(d0 == "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-\xe5\x85\xa5\xe5\x8f\xa3",
          "index 0 description round-trips as UTF-8");
    check(c0 == "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86",
          "index 0 category is 斯坦索姆");

    double x = 0, y = 0, z = 0, r = -1;
    int32_t has_r = 42;
    check(th_favourites_get_position(session, 0, &x, &y, &z, &r, &has_r) == 0,
          "position getter succeeds");
    check(x == 1.0 && y == 2.0 && z == 3.0, "coordinates parsed correctly");
    check(has_r == 0, "no orientation reports has_r=0");
    check(r == -1.0, "no orientation leaves r untouched");

    // Index 2 -> orientation present.
    x = y = z = r = 0;
    has_r = 0;
    check(th_favourites_get_position(session, 2, &x, &y, &z, &r, &has_r) == 0,
          "orientation entry position getter succeeds");
    check(has_r == 1, "explicit orientation reports has_r=1");
    check(std::fabs(r - 1.857) < 1e-6, "orientation value parsed");

    // Index 1 -> OTHER_CATEGORY ("其他") since there's no dash.
    const std::string c1 = read_indexed(session, 1, th_favourites_get_category);
    check(c1 == "\xe5\x85\xb6\xe4\xbb\x96", "no-dash entry falls back to 其他 category");

    // Out-of-range and NULL guards.
    check(th_favourites_get_description(session, -1, nullptr, 0) == -1,
          "negative index rejected");
    check(th_favourites_get_description(session, 999, nullptr, 0) == -1,
          "out-of-range index rejected");
    check(th_favourites_load(session, nullptr) == -1, "NULL path rejected");

    // Missing file: repository treats as empty list (not an error). Verify the
    // C ABI mirrors that: load() succeeds with count==0.
    const std::filesystem::path missing = dir / "does_not_exist.fav";
    check(th_favourites_load(session, missing.string().c_str()) == 0,
          "missing favlist load returns success with empty list");
    check(th_favourites_count(session) == 0,
          "missing favlist load leaves zero points");

    th_session_destroy(session);
}

void test_favourites_crud_and_save(const std::filesystem::path& dir) {
    // Fresh session with the same three-entry favlist.fav from the previous
    // test, then exercise every CRUD helper.
    const std::filesystem::path source = dir / "capi_smoke.fav";
    {
        std::ofstream out(source, std::ios::binary);
        out << "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-"
            << "\xe5\x85\xa5\xe5\x8f\xa3#1#2#3#\n";
        out << "\xe6\x97\xa0\xe5\x88\x86\xe7\xb1\xbb#4#5#6#\n";
        out << "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-"
            << "\xe5\x87\xba\xe5\x8f\xa3#7#8#9#1.857\n";
    }

    th_session* session = th_session_create();
    if (!session) return;
    check(th_favourites_load(session, source.string().c_str()) == 0, "reload favlist");

    // Two source categories: 斯坦索姆 (2 entries) and 其他 (1 entry). Order
    // reflects first-appearance, so 斯坦索姆 comes first.
    check(th_favourites_category_count(session) == 2, "two source categories");
    const std::string cat0 = read_indexed(session, 0, th_favourites_category_name);
    const std::string cat1 = read_indexed(session, 1, th_favourites_category_name);
    check(cat0 == "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86",
          "category 0 is 斯坦索姆 (first-seen)");
    check(cat1 == "\xe5\x85\xb6\xe4\xbb\x96", "category 1 is 其他");

    // Insert a new 斯坦索姆-中间 point between the existing 入口/出口 rows.
    // Category-local index 1 means "before the second 斯坦索姆 entry"
    // (i.e. 出口, which sits at global index 2). insert_in_category
    // therefore lands the new row at global index 2 and bumps the old
    // 出口 to global 3.
    const char* insertedName =
        "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-\xe4\xb8\xad\xe9\x97\xb4"; // 斯坦索姆-中间
    const char* stCategory = "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86";     // 斯坦索姆
    check(th_favourites_insert_in_category(session, stCategory, 1, insertedName,
                                             10.0, 20.0, 30.0, 0, 0.0) == 0,
          "insert before second 斯坦索姆 row");
    check(th_favourites_count(session) == 4, "insert grew the list by one");
    check(read_indexed(session, 2, th_favourites_get_description) == insertedName,
          "insertion landed at global index 2 (before the old 出口)");

    // Replace that same row with different coords + explicit orientation.
    // In 斯坦索姆's category-local ordering, local index 1 = 中间 (global 2).
    check(th_favourites_replace_in_category(session, stCategory, 1, insertedName,
                                              11.0, 22.0, 33.0, 1, 2.5) == 0,
          "replace second 斯坦索姆 row");
    double x = 0, y = 0, z = 0, r = 0;
    int32_t has_r = 0;
    check(th_favourites_get_position(session, 2, &x, &y, &z, &r, &has_r) == 0,
          "replaced row's position readable");
    check(x == 11.0 && y == 22.0 && z == 33.0, "replaced row has new coords");
    check(has_r == 1 && std::fabs(r - 2.5) < 1e-6, "replaced row carries orientation");

    // Append after the last 斯坦索姆 entry. The new description must live in
    // the same category (斯坦索姆) or the category-local delete below will
    // look for it in the wrong bucket.
    const char* appendedName =
        "\xe6\x96\xaf\xe5\x9d\xa6\xe7\xb4\xa2\xe5\xa7\x86-\xe5\xa4\x96"; // 斯坦索姆-外
    check(th_favourites_append_in_category(session, stCategory, 2, appendedName,
                                             99.0, 88.0, 77.0, 0, 0.0) == 0,
          "append after last 斯坦索姆 row");
    check(th_favourites_count(session) == 5, "append grew the list by one");

    // Delete the row we just appended (last 斯坦索姆 entry, local index 3).
    check(th_favourites_delete_in_category(session, stCategory, 3) == 0,
          "delete the appended row");
    check(th_favourites_count(session) == 4, "delete shrunk the list by one");

    // Failure paths leave the list unchanged. Save the current state and
    // check byte-for-byte after a bad mutation.
    const std::filesystem::path baseline = dir / "before_bad.fav";
    check(th_favourites_save(session, baseline.string().c_str()) == 4,
          "save reports the written count");
    check(th_favourites_delete_in_category(session, stCategory, 99) == -1,
          "out-of-range delete rejected");
    check(th_favourites_delete_in_category(session, TH_CAPI_ALL_CATEGORY, 99) == -1,
          "out-of-range delete in synthetic all-category is rejected safely");
    check(th_favourites_insert_in_category(session, TH_CAPI_ALL_CATEGORY, -1, "bad",
                                             0, 0, 0, 0, 0) == -1,
          "negative insert index in synthetic all-category is rejected safely");
    check(th_favourites_count(session) == 4,
          "failed mutations leave the in-memory list unchanged");
    const std::filesystem::path after_bad = dir / "after_bad.fav";
    check(th_favourites_save(session, after_bad.string().c_str()) == 4,
          "save after failed op still reports full count");
    check(!std::filesystem::exists(after_bad.string() + ".tmp"),
          "successful atomic save leaves no temporary file");

    std::ifstream baselineStream(baseline, std::ios::binary);
    std::ifstream afterStream(after_bad, std::ios::binary);
    std::stringstream baselineBuffer, afterBuffer;
    baselineBuffer << baselineStream.rdbuf();
    afterBuffer << afterStream.rdbuf();
    check(baselineBuffer.str() == afterBuffer.str(),
          "failed mutation produces a byte-for-byte identical save output");

    // Round-trip: reload the saved file into a fresh session and confirm.
    th_session* fresh = th_session_create();
    if (fresh)
    {
        check(th_favourites_load(fresh, baseline.string().c_str()) == 0,
              "saved favlist reloads");
        check(th_favourites_count(fresh) == 4,
              "reloaded count matches the saved list");
        th_session_destroy(fresh);
    }

    th_session_destroy(session);
}

void test_null_and_dangling_argument_guards() {
    // NULL session must not crash any entry point.
    check(th_session_is_attached(nullptr) == -1, "is_attached rejects NULL session");
    check(th_session_pid(nullptr) == 0, "pid returns 0 for NULL session");
    check(th_session_set_version(nullptr, "3.3.5") == -1,
          "set_version rejects NULL session");
    char buf[8] = {0};
    check(th_session_last_error(nullptr, buf, sizeof(buf)) == -1,
          "last_error rejects NULL session");
    check(th_process_list(nullptr, nullptr, 0) == -1,
          "process_list rejects NULL executable");
}

} // namespace

int main() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "teleporthackdx-capi-tests";
    std::filesystem::create_directories(dir);

    test_version_enumeration();
    test_buffer_truncation();
    test_session_lifecycle_defaults();
    test_attached_operations_fail_gracefully();
    test_favourites_from_file(dir);
    test_favourites_crud_and_save(dir);
    test_null_and_dangling_argument_guards();

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "C ABI tests passed\n";
    return 0;
}
