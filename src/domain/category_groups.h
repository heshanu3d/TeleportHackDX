// "分类导航" groups, ported from TeleportHackOnVanilla's CategoryGridGroup()
// and RefreshCategoryGrid() (ui.au3, f34e471/d25a20f).
//
// Categories are bucketed by zone name into five fixed groups; anything not
// listed (主城, 副本60, 其他, ...) lands in group 0 "杂项", which the
// navigation grid also prefixes with the synthetic "所有" entry. Within a
// group, categories keep the favourites' first-appearance order.
#pragma once

#include <string>
#include <vector>

namespace th {

constexpr int kCategoryGroupCount = 5;

// UTF-8 display name of `group` ("杂项", "东部王国", "卡利姆多",
// "燃烧的远征", "巫妖王之怒"); empty for an out-of-range group.
const std::string& category_group_name(int group);

// Group index (0..kCategoryGroupCount-1) for a category name.
int category_group_of(const std::string& category);

struct CategoryGroup {
    std::string name;
    std::vector<std::string> categories;
};

// Buckets `categories` (in their given order) into all five groups, empty
// groups included. Group 0 starts with ALL_CATEGORY ("所有").
std::vector<CategoryGroup> group_categories(const std::vector<std::string>& categories);

} // namespace th
