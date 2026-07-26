// Update or insert a coordinate entry inside the server-side Teleport.lua
// script's `local FAV = { {"name", mapId, x, y, z, o}, ... }` table.
//
// Teleport.lua is normally regenerated wholesale from favlist.fav by the
// sibling gen_fav.py script (mapId inference, menu code, etc.). This
// repository instead performs surgical single-row updates/inserts for the
// "读取到炉石" and "插后 表&炉石" buttons, without re-running gen_fav.py or
// touching anything else in the file (menu code, unrelated entries, etc.).
//
// The file is UTF-8 with a BOM and CRLF line endings (see gen_fav.py's
// `io.open(..., encoding="utf-8-sig")` / `"\r\n".join(...)`); both are
// preserved on save so the file stays byte-for-byte identical apart from
// the one edited line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "domain/models.h"

namespace th {

class TeleportLuaRepository {
public:
    explicit TeleportLuaRepository(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }

    // Finds the FAV entry whose name exactly matches `description` inside
    // the `local FAV = { ... }` table and overwrites its x/y/z (always)
    // and o (only if `pos.orientation` has a value -- a missing
    // orientation leaves the existing o field untouched, same
    // preserve-current-facing convention used for favlist.fav). Returns
    // false with `error` set if the file/table/entry can't be found.
    bool update_entry(const std::string& description, const Position& pos,
                       std::string& error) const;

    // Locates the zero-based FAV row `after_index`, verifies its decoded name
    // matches `after_description`, then inserts immediately after it. The
    // index disambiguates duplicate descriptions and the name check detects
    // a Teleport.lua that is out of sync with favlist.fav.
    bool insert_entry_after(size_t after_index, const std::string& after_description,
                            const TeleportPoint& point, uint32_t map_id,
                            std::string& error) const;

private:
    std::string path_;
};

} // namespace th
