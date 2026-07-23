// Read/write the AutoIt-compatible favlist.fav file.
//
// Format (one teleport per line): "描述#x#y#z"
// Legacy 4-line-per-record layout is tolerated on read, but save() always
// emits the canonical single-line form.
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
