#include "services/favourites_service.h"

#include <sstream>

namespace th {

bool FavouritesService::reload(std::string& error) { return repo_.load(points_, error); }

int FavouritesService::save(std::string& error) { return repo_.save(points_, error); }

std::vector<Category> FavouritesService::categories() const {
    return points_to_categories(points_);
}

std::vector<std::string> FavouritesService::category_names() const {
    std::vector<std::string> names;
    for (const auto& c : categories()) names.push_back(c.name);
    return names;
}

std::vector<TeleportPoint> FavouritesService::points_in_category(const std::string& name) const {
    if (name == ALL_CATEGORY) return points_;
    std::vector<TeleportPoint> result;
    for (const auto& p : points_) {
        if (p.category() == name) result.push_back(p);
    }
    return result;
}

const TeleportPoint* FavouritesService::find_by_name(const std::string& description) const {
    for (const auto& p : points_) {
        if (p.description == description) return &p;
    }
    return nullptr;
}

void FavouritesService::add(const TeleportPoint& point) { points_.push_back(point); }

bool FavouritesService::global_index(const std::string& category, int local_index,
                                      bool allow_end, int& out, std::string& error) const {
    if (category == ALL_CATEGORY) {
        out = local_index;
        return true;
    }
    int seen = 0;
    for (size_t i = 0; i < points_.size(); ++i) {
        if (points_[i].category() != category) continue;
        if (seen == local_index) {
            out = static_cast<int>(i);
            return true;
        }
        ++seen;
    }
    if (allow_end && local_index == seen) {
        out = static_cast<int>(points_.size());
        return true;
    }
    std::ostringstream ss;
    ss << "Index " << local_index << " out of range in category '" << category
       << "' (size=" << seen << ")";
    error = ss.str();
    return false;
}

bool FavouritesService::insert_in_category(const std::string& category, int index,
                                            const TeleportPoint& point, std::string& error) {
    int gi;
    if (!global_index(category, index, true, gi, error)) return false;
    points_.insert(points_.begin() + gi, point);
    return true;
}

bool FavouritesService::append_in_category(const std::string& category, int index,
                                            const TeleportPoint& point, std::string& error) {
    return insert_in_category(category, index + 1, point, error);
}

bool FavouritesService::replace_in_category(const std::string& category, int index,
                                             const TeleportPoint& point, std::string& error) {
    int gi;
    if (!global_index(category, index, false, gi, error)) return false;
    points_[gi] = point;
    return true;
}

bool FavouritesService::delete_in_category(const std::string& category, int index,
                                            TeleportPoint& removed, std::string& error) {
    int gi;
    if (!global_index(category, index, false, gi, error)) return false;
    removed = points_[gi];
    points_.erase(points_.begin() + gi);
    return true;
}

} // namespace th
