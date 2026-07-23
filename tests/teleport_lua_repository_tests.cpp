// TeleportLuaRepository tests: surgical single-entry edits inside a
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
    bool ok = repo.update_entry(kNameNormal, th::Position{100, 200, 300, 4.5}, error);
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
        repo.update_entry(kNameWithOrientation, th::Position{10, 20, 30, std::nullopt}, error);
    check(ok, "update_entry succeeds even without an orientation");

    std::string content = read_file(path);
    check(content.find(", 601, 10, 20, 30, 1.857},") != std::string::npos,
          "missing orientation leaves the existing o field (601's mapId also untouched)");
}

void test_unknown_name_fails_gracefully(const std::filesystem::path& dir) {
    auto path = dir / "unknown.lua";
    write_file(path, make_fixture());

    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.update_entry("does-not-exist", th::Position{1, 2, 3, std::nullopt}, error);
    check(!ok, "unknown name returns false instead of silently doing nothing");
    check(!error.empty(), "a descriptive error is set");

    std::string content = read_file(path);
    check(content == make_fixture(), "file is left completely untouched on failure");
}

void test_missing_file(const std::filesystem::path& dir) {
    th::TeleportLuaRepository repo((dir / "does_not_exist.lua").string());
    std::string error;
    bool ok = repo.update_entry("anything", th::Position{1, 2, 3, std::nullopt}, error);
    check(!ok, "missing file returns false");
    check(!error.empty(), "missing file sets a descriptive error");
}

void test_missing_fav_table(const std::filesystem::path& dir) {
    auto path = dir / "no_fav_table.lua";
    write_file(path, "print(\"just some other lua file\")\n");
    th::TeleportLuaRepository repo(path.string());
    std::string error;
    bool ok = repo.update_entry("anything", th::Position{1, 2, 3, std::nullopt}, error);
    check(!ok, "file without a 'local FAV = {' table returns false");
    check(!error.empty(), "missing FAV table sets a descriptive error");
}

} // namespace

int main() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "teleporthackdx-teleport-lua-tests";
    std::filesystem::create_directories(dir);

    test_basic_update(dir);
    test_missing_orientation_preserves_existing_o(dir);
    test_unknown_name_fails_gracefully(dir);
    test_missing_file(dir);
    test_missing_fav_table(dir);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "teleport_lua_repository tests passed\n";
    return 0;
}
