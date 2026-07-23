// Manage the in-memory favourites collection plus persistence.
//
// Ported from
// TeleportHackOnVanilla/win/src/teleport_hack/application/favourites_service.py
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/models.h"
#include "repository/favlist_repository.h"

namespace th {

class FavouritesService {
public:
    explicit FavouritesService(FavlistRepository repository) : repo_(std::move(repository)) {}

    bool reload(std::string& error);
    int save(std::string& error); // returns count written, -1 on failure

    const std::vector<TeleportPoint>& all_points() const { return points_; }

    // Categories in original insertion order (excludes the synthetic "所有").
    std::vector<Category> categories() const;
    std::vector<std::string> category_names() const;
    std::vector<TeleportPoint> points_in_category(const std::string& name) const;

    const TeleportPoint* find_by_name(const std::string& description) const;

    void add(const TeleportPoint& point);
    void replace_all(std::vector<TeleportPoint> points) { points_ = std::move(points); }
    // Insert before the visible row `index` within `category`. Returns
    // false + sets error on out-of-range index.
    bool insert_in_category(const std::string& category, int index, const TeleportPoint& point,
                             std::string& error);
    bool append_in_category(const std::string& category, int index, const TeleportPoint& point,
                             std::string& error);
    bool replace_in_category(const std::string& category, int index, const TeleportPoint& point,
                              std::string& error);
    bool delete_in_category(const std::string& category, int index, TeleportPoint& removed,
                             std::string& error);

private:
    FavlistRepository repo_;
    std::vector<TeleportPoint> points_;

    // allow_end: if true, `local_index == size` is accepted as "append at end".
    bool global_index(const std::string& category, int local_index, bool allow_end, int& out,
                       std::string& error) const;
};

} // namespace th
