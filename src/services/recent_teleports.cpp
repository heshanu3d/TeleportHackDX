#include "services/recent_teleports.h"

#include <algorithm>

namespace th {

namespace {
const std::string kDefaultLabel = "\xe5\x9d\x90\xe6\xa0\x87\xe7\x82\xb9"; // 坐标点

// Same identity as the AutoIt list: coordinates plus map, ignoring label
// and facing.
bool same_place(const Position& a, const Position& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.map_id == b.map_id;
}
} // namespace

void RecentTeleports::record(const std::string& label, const Position& position) {
    auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const RecentTeleport& e) {
        return same_place(e.position, position);
    });
    if (existing != entries_.end()) entries_.erase(existing);
    entries_.insert(entries_.begin(), RecentTeleport{label.empty() ? kDefaultLabel : label,
                                                     position});
    if (entries_.size() > kLimit) entries_.resize(kLimit);
}

bool RecentTeleports::get(size_t index, RecentTeleport& out) const {
    if (index >= entries_.size()) return false;
    out = entries_[index];
    return true;
}

} // namespace th
