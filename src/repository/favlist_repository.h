// Read/write the AutoIt-compatible favlist.fav file.
//
// Canonical format (one teleport per line): "描述#x#y#z#r" where `r` is an
// optional orientation/facing in radians. `r` may be an empty field
// (meaning "no orientation, preserve current facing" -- distinct from an
// explicit `r=0`). The old 4-field format ("描述#x#y#z", no orientation)
// and legacy 4/5-line-per-record layouts are all tolerated on read; save()
// always emits the canonical 5-field single-line form.
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/infrastructure/repository/favlist.py
#pragma once

#include <string>
#include <vector>

#include "domain/models.h"

namespace th {

class FavlistRepository {
public:
    explicit FavlistRepository(std::string path, char separator = '#')
        : path_(std::move(path)), sep_(separator) {}

    const std::string& path() const { return path_; }

    // Returns false with `error` set if the file exists but can't be read.
    // A missing file yields an empty list (not an error).
    bool load(std::vector<TeleportPoint>& out, std::string& error) const;

    // Overwrites the file with `points` in canonical form. Returns the
    // number of records written, or -1 on failure (see `error`).
    int save(const std::vector<TeleportPoint>& points, std::string& error) const;

private:
    std::string path_;
    char sep_;
};

} // namespace th
