// TeleportLuaRepository tests: surgical single-entry edits/inserts inside a
// server-side Teleport.lua's `local FAV = { {"name", mapId, x, y, z, o},
// ... }` table, preserving everything else in the file byte-for-byte
// (BOM, CRLF, menu code, other entries, mapId).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "domain/models.h"
#include "repository/teleport_lua_repository.h"
#include "services/favourites_service.h"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

const char* kNameNormal = "主城-暴风城-英雄谷";
const char* kNameWithOrientation = "副本80-艾卓尼鲁布-老1贴";

// A small but structurally faithful fixture: UTF-8 BOM, CRLF line endings,
// unrelated menu code before/after the FAV table, and a handful of rows
// including one whose name contains an escaped quote (edge case).
std::string make_fixture() {
    std::string s;
    s += "\xEF\xBB\xBF"; // BOM
    s += "print(\"Teleport.lua start loading\")\r\n";
    s += "\r\n";
    s += "local itemid =6948\r\n";
    s += "-- @gen_fav BEGIN@\r\n";
    s += "local FAV_DEBUG = true\r\n";
    s += "local FAV = { -- {名称, mapId, X, Y, Z, 朝向O}\r\n";
    s += std::string("    {\"") + kNameNormal + "\", 0, -9023.236, 465.42, 94.621, 0},\r\n";
    s += "    {\"quote-\\\"test\\\"\", 0, 1, 2, 3, 0},\r\n"; // name with an escaped quote
    s += std::string("    {\"") + kNameWithOrientation + "\", 601, 531.796, 642.249, 777.303, 1.857},\r\n";
    s += "}\r\n";
    s += "\r\n";
    s += "-- rest of the menu code, must survive untouched\r\n";
    s += "-- @gen_fav END@\r\n";
    s += "print(\"Teleport.lua loaded\")\r\n";
    return s;
}

void test_basic_update(const std::filesystem::path& dir) {
    auto path = dir / "basic.lua";
    write_file(path, make_fixture());

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    // orientation present -> should overwrite o too
    bool ok = repo.update_entry(kNameNormal, 0, th::Position{100, 200, 300, 4.5}, error);
    check(ok, "update_entry succeeds for a matching name");

    std::string content = read_file(path);
    check(content.rfind("\xEF\xBB\xBF", 0) == 0, "BOM is preserved");
    check(content.find("\r\n") != std::string::npos, "CRLF line endings are preserved");
    check(content.find(std::string("{\"") + kNameNormal + "\", 0, 100, 200, 300, 4.5},") !=
              std::string::npos,
          "matched row's x/y/z/o are updated, name+mapId untouched");
    check(content.find("quote-\\\"test\\\"") != std::string::npos,
          "unrelated row with an escaped quote in its name is untouched");
    check(content.find("-- rest of the menu code, must survive untouched") != std::string::npos,
          "code outside the FAV table survives untouched");
}

void test_missing_orientation_preserves_existing_o(const std::filesystem::path& dir) {
    auto path = dir / "preserve_o.lua";
    write_file(path, make_fixture());

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    // orientation NOT present -> existing o (1.857) must survive untouched
    bool ok =
        repo.update_entry(kNameWithOrientation, 0,
                          th::Position{10, 20, 30, std::nullopt}, error);
    check(ok, "update_entry succeeds even without an orientation");

    std::string content = read_file(path);
    check(content.find(", 601, 10, 20, 30, 1.857},") != std::string::npos,
          "missing orientation leaves the existing o field (601's mapId also untouched)");
}

void test_insert_after(const std::filesystem::path& dir) {
    auto path = dir / "insert.lua";
    write_file(path, make_fixture());

    th::TeleportLuaRepository repo(path.string());
    const std::string new_name = "新增-\\path-\"quoted\"";
    std::string error;
    bool ok = repo.insert_entry_after(
        kNameNormal, 0, th::TeleportPoint{new_name, th::Position{100, 200, 300, 4.5}}, 571,
        error);
    check(ok, "insert_entry_after succeeds for a matching anchor");

    std::string content = read_file(path);
    check(content.rfind("\xEF\xBB\xBF", 0) == 0, "insert preserves BOM");
    check(content.find("\r\n") != std::string::npos, "insert preserves CRLF line endings");
    const std::string expected_sequence =
        std::string("{\"") + kNameNormal + "\", 0, -9023.236, 465.42, 94.621, 0},\r\n" +
        "    {\"新增-\\\\path-\\\"quoted\\\"\", 571, 100, 200, 300, 4.5},\r\n" +
        "    {\"quote-\\\"test\\\"\", 0, 1, 2, 3, 0},";
    check(content.find(expected_sequence) != std::string::npos,
          "new row is immediately after the anchor with escaped name and current map/xyzo");
    check(content.find("-- rest of the menu code, must survive untouched") != std::string::npos,
          "insert leaves code outside the FAV table untouched");
    check(!std::filesystem::exists(path.string() + ".tmp"),
          "successful atomic save leaves no temporary file behind");

    // The inserted escaped name must decode back to the original description
    // so later 读取到炉石 updates can still find it.
    ok = repo.update_entry(new_name, 0, th::Position{101, 201, 301, 4.6}, error);
    check(ok, "an inserted escaped name can be found and updated later");
    content = read_file(path);
    check(content.find("{\"新增-\\\\path-\\\"quoted\\\"\", 571, 101, 201, 301, 4.6},") !=
              std::string::npos,
          "later update preserves inserted name/mapId and changes xyzo");

    ok = repo.insert_entry_after(new_name, 0,
                                  th::TeleportPoint{"no-r", th::Position{1, 2, 3, std::nullopt}},
                                  1, error);
    check(ok, "insert succeeds without an orientation");
    content = read_file(path);
    check(content.find("{\"no-r\", 1, 1, 2, 3, 0},") != std::string::npos,
          "missing orientation is written as zero for a new Lua row");
}

void test_insert_bad_anchor_leaves_file_untouched(const std::filesystem::path& dir) {
    auto path = dir / "insert_unknown.lua";
    write_file(path, make_fixture());

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.insert_entry_after(
        "does-not-exist", 0, th::TeleportPoint{"new", th::Position{1, 2, 3, 0.0}}, 0, error);
    check(!ok, "insert with an out-of-range anchor returns false");
    check(!error.empty(), "out-of-range insert anchor sets a descriptive error");
    check(read_file(path) == make_fixture(), "failed insert leaves the file byte-for-byte intact");
}

void test_dynamic_anchor_uses_name_occurrence(const std::filesystem::path& dir) {
    auto path = dir / "duplicate.lua";
    const std::string fixture =
        "local FAV = {\n"
        "    {\"dup\", 0, 1, 2, 3, 0},\n"
        "    {\"lua-only\", 530, 20, 21, 22, 0},\n"
        "    {\"middle\", 0, 4, 5, 6, 0},\n"
        "    {\"dup\", 1, 7, 8, 9, 0},\n"
        "}\n";
    write_file(path, fixture);

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.insert_entry_after(
        "dup", 1, th::TeleportPoint{"after-second", th::Position{10, 11, 12, 1.5}}, 571, error);
    check(ok, "duplicate anchor can be selected by its name occurrence");
    std::string content = read_file(path);
    check(content.find("{\"dup\", 1, 7, 8, 9, 0},\n"
                       "    {\"after-second\", 571, 10, 11, 12, 1.5},") != std::string::npos,
          "insert targets the selected duplicate despite unrelated Lua-only rows");

    ok = repo.update_entry("dup", 1, th::Position{70, 80, 90, 2.5}, error);
    check(ok, "second duplicate can be updated by occurrence");
    content = read_file(path);
    check(content.find("{\"dup\", 1, 70, 80, 90, 2.5},") != std::string::npos,
          "dynamic update targets the selected duplicate");

    const std::string before_missing = content;
    ok = repo.insert_entry_after(
        "dup", 2, th::TeleportPoint{"wrong", th::Position{1, 2, 3, 0.0}}, 0, error);
    check(!ok, "missing duplicate occurrence is rejected");
    check(read_file(path) == before_missing, "missing occurrence leaves the file untouched");
}

void test_category_append_keeps_global_order(const std::filesystem::path& dir) {
    th::FavouritesService favourites{th::FavlistRepository((dir / "order.fav").string())};
    favourites.replace_all({
        {"A-one", th::Position{1, 1, 1, std::nullopt}},
        {"A-two", th::Position{2, 2, 2, std::nullopt}},
        {"B-one", th::Position{3, 3, 3, std::nullopt}},
    });

    std::string error;
    int global_index = -1;
    check(favourites.resolve_global_index("A", 1, global_index, error) && global_index == 1,
          "category-local selection resolves to its global favlist index");
    size_t occurrence = 99;
    check(favourites.resolve_description_occurrence("A", 1, occurrence, error) && occurrence == 0,
          "unique selected description resolves to occurrence zero");
    check(favourites.append_in_category(
              "A", 1, th::TeleportPoint{"B-new", th::Position{4, 4, 4, std::nullopt}}, error),
          "append after the last item in a category succeeds");
    const auto& points = favourites.all_points();
    check(points.size() == 4 && points[2].description == "B-new" &&
              points[3].description == "B-one",
          "category append inserts directly after the selected global row, not at file end");

    th::FavouritesService duplicates{th::FavlistRepository((dir / "duplicates.fav").string())};
    duplicates.replace_all({
        {"dup", th::Position{1, 1, 1, std::nullopt}},
        {"other", th::Position{2, 2, 2, std::nullopt}},
        {"dup", th::Position{3, 3, 3, std::nullopt}},
    });
    check(duplicates.resolve_description_occurrence(th::ALL_CATEGORY, 2, occurrence, error) &&
              occurrence == 1,
          "second duplicate resolves to occurrence one independently of global row index");
}

void test_unknown_name_fails_gracefully(const std::filesystem::path& dir) {
    auto path = dir / "unknown.lua";
    write_file(path, make_fixture());

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.update_entry("does-not-exist", 0,
                                th::Position{1, 2, 3, std::nullopt}, error);
    check(!ok, "unknown name returns false instead of silently doing nothing");
    check(!error.empty(), "a descriptive error is set");

    std::string content = read_file(path);
    check(content == make_fixture(), "file is left completely untouched on failure");
}

void test_missing_file(const std::filesystem::path& dir) {
    th::TeleportLuaRepository repo((dir / "does_not_exist.lua").string());
    std::string error;
    bool ok = repo.update_entry("anything", 0, th::Position{1, 2, 3, std::nullopt}, error);
    check(!ok, "missing file returns false");
    check(!error.empty(), "missing file sets a descriptive error");
}

void test_missing_fav_table(const std::filesystem::path& dir) {
    auto path = dir / "no_fav_table.lua";
    write_file(path, "print(\"just some other lua file\")\n");
    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.update_entry("anything", 0, th::Position{1, 2, 3, std::nullopt}, error);
    check(!ok, "file without a 'local FAV = {' table returns false");
    check(!error.empty(), "missing FAV table sets a descriptive error");
}

void test_atomic_save_failure_preserves_original(const std::filesystem::path& dir) {
    auto path = dir / "atomic_failure.lua";
    write_file(path, make_fixture());
    std::filesystem::create_directory(path.string() + ".tmp");

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.update_entry(kNameNormal, 0, th::Position{1, 2, 3, 4}, error);
    check(!ok, "failure to create the temporary output returns false");
    check(!error.empty(), "temporary output failure sets a descriptive error");
    check(read_file(path) == make_fixture(), "temporary output failure preserves the live Lua file");

    std::error_code ignored;
    std::filesystem::remove(path.string() + ".tmp", ignored);
}

} // namespace

int main() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "teleporthackdx-teleport-lua-tests";
    std::filesystem::create_directories(dir);

    test_basic_update(dir);
    test_missing_orientation_preserves_existing_o(dir);
    test_insert_after(dir);
    test_insert_bad_anchor_leaves_file_untouched(dir);
    test_dynamic_anchor_uses_name_occurrence(dir);
    test_category_append_keeps_global_order(dir);
    test_unknown_name_fails_gracefully(dir);
    test_missing_file(dir);
    test_missing_fav_table(dir);
    test_atomic_save_failure_preserves_original(dir);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "teleport_lua_repository tests passed\n";
    return 0;
}
