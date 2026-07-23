#include "services/favourites_porter.h"

#include <optional>
#include <set>
#include <tuple>

namespace th {

namespace {
using Signature = std::tuple<std::string, double, double, double, std::optional<double>>;

Signature signature_of(const TeleportPoint& p) {
    return {p.description, p.position.x, p.position.y, p.position.z, p.position.orientation};
}
} // namespace

int FavouritesPorter::export_to(const std::string& path, std::string& error) const {
    FavlistRepository repo(path);
    return repo.save(service_.all_points(), error);
}

int FavouritesPorter::import_replace(const std::string& path, std::string& error) const {
    FavlistRepository repo(path);
    std::vector<TeleportPoint> loaded;
    if (!repo.load(loaded, error)) return -1;
    int count = static_cast<int>(loaded.size());
    service_.replace_all(std::move(loaded));
    return count;
}

MergeReport FavouritesPorter::merge_points(const std::vector<TeleportPoint>& incoming) const {
    std::set<Signature> existing;
    for (const auto& p : service_.all_points()) existing.insert(signature_of(p));

    MergeReport report;
    for (const auto& pt : incoming) {
        Signature sig = signature_of(pt);
        if (existing.count(sig)) {
            ++report.skipped_duplicates;
            continue;
        }
        existing.insert(sig);
        service_.add(pt);
        ++report.added;
    }
    return report;
}

bool FavouritesPorter::import_merge(const std::string& path, MergeReport& report,
                                     std::string& error) const {
    FavlistRepository repo(path);
    std::vector<TeleportPoint> incoming;
    if (!repo.load(incoming, error)) return false;
    report = merge_points(incoming);
    return true;
}

} // namespace th
