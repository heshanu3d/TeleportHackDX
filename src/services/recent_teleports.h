// "最近使用的坐标": the last successful teleports, most recent first.
//
// Ported from TeleportHackOnVanilla's RecordRecentTeleport() (ui.au3,
// d25a20f/d462c4c): at most 20 entries kept in memory only; teleporting to
// the same x/y/z on the same map again moves that entry to the top (with
// the new label/facing) instead of adding a duplicate. An empty label is
// shown as "坐标点".
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "domain/models.h"

namespace th {

struct RecentTeleport {
    std::string label;
    Position position;
};

class RecentTeleports {
public:
    static constexpr size_t kLimit = 20;

    void record(const std::string& label, const Position& position);

    const std::vector<RecentTeleport>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }
    // Returns false if `index` is out of range.
    bool get(size_t index, RecentTeleport& out) const;
    void clear() { entries_.clear(); }

private:
    std::vector<RecentTeleport> entries_;
};

} // namespace th
