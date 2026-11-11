#include "ui/category_selection.h"

#include <algorithm>

namespace th {
bool CategorySelection::contains(const std::string& item) const {
    return std::find(items_.begin(), items_.end(), item) != items_.end();
}
void CategorySelection::clear() {
    items_.clear();
    anchor_.clear();
    has_anchor_ = false;
}
void CategorySelection::retain(const std::vector<std::string>& visible) {
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const std::string& item) {
        return std::find(visible.begin(), visible.end(), item) == visible.end();
    }), items_.end());
    if (has_anchor_ && std::find(visible.begin(), visible.end(), anchor_) == visible.end()) {
        has_anchor_ = false;
        anchor_.clear();
    }
}
void CategorySelection::click(const std::vector<std::string>& visible, std::size_t index,
                              bool ctrl, bool shift) {
    if (index >= visible.size()) return;
    const auto anchor_it = has_anchor_ ? std::find(visible.begin(), visible.end(), anchor_) : visible.end();
    if (shift && anchor_it != visible.end()) {
        const std::size_t other = static_cast<std::size_t>(anchor_it - visible.begin());
        if (!ctrl) items_.clear();
        for (std::size_t i = std::min(index, other); i <= std::max(index, other); ++i)
            if (!contains(visible[i])) items_.push_back(visible[i]);
        return;
    }
    const std::string& item = visible[index];
    if (!ctrl) items_.clear();
    const auto found = std::find(items_.begin(), items_.end(), item);
    if (found == items_.end()) items_.push_back(item);
    else if (ctrl) items_.erase(found);
    anchor_ = item;
    has_anchor_ = true;
}
}
