// Import / export / merge favourites with another file.
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/favourites_porter.py
#pragma once

#include <string>
#include <vector>

#include "repository/favlist_repository.h"
#include "services/favourites_service.h"

namespace th {

struct MergeReport {
    int added = 0;
    int skipped_duplicates = 0;
    int total_examined() const { return added + skipped_duplicates; }
};

class FavouritesPorter {
public:
    explicit FavouritesPorter(FavouritesService& service) : service_(service) {}

    // Writes the *current* favourites to `path`. Returns count, -1 on error.
    int export_to(const std::string& path, std::string& error) const;

    // Replaces all favourites with the contents of `path`. Returns count
    // loaded, -1 on error.
    int import_replace(const std::string& path, std::string& error) const;

    // Merges contents of `path` into current favourites, skipping exact
    // (description, x, y, z) duplicates.
    bool import_merge(const std::string& path, MergeReport& report, std::string& error) const;

    MergeReport merge_points(const std::vector<TeleportPoint>& incoming) const;

private:
    FavouritesService& service_;
};

} // namespace th
