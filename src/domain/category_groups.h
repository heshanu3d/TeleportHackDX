// "分类导航" groups, ported from TeleportHackOnVanilla's CategoryGridGroup()
// and RefreshCategoryGrid() (ui.au3, f34e471/d25a20f).
//
// Categories are bucketed by zone name into five fixed groups; anything not
// listed (主城, 副本60, 其他, ...) lands in group 0 "杂项", which the
// navigation grid also prefixes with the synthetic "所有" entry. Within a
// known group, categories follow the source project's preset order. Misc
// retains the favourites' first-appearance order.
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

// Preset ordering key, or -1 for a miscellaneous category.
int category_order(const std::string& category);

struct CategoryGroup {
    std::string name;
    std::vector<std::string> categories;
};

// Uses the preset zone order, keeping empty groups. Group 0 starts with
// ALL_CATEGORY ("所有") and retains the original input order.
std::vector<CategoryGroup> group_categories(const std::vector<std::string>& categories);

// User-defined groups retain assignments for categories absent from the
// currently loaded file; navigation only shows categories present there.
bool valid_category_group_name(const std::string& name);
bool valid_category_group_config(const std::vector<CategoryGroup>& groups);
std::vector<CategoryGroup> editable_category_groups(const std::vector<std::string>& categories,
                                                     const std::vector<CategoryGroup>& saved,
                                                     bool customized);
std::vector<CategoryGroup> navigation_category_groups(const std::vector<std::string>& categories,
                                                       const std::vector<CategoryGroup>& saved,
                                                       bool customized);
bool add_category_group(std::vector<CategoryGroup>& groups, const std::string& name);
bool rename_category_group(std::vector<CategoryGroup>& groups, size_t index, const std::string& name);
bool delete_category_group(std::vector<CategoryGroup>& groups, size_t index);
bool move_category_group(std::vector<CategoryGroup>& groups, size_t from, size_t to);
bool move_category_between_groups(std::vector<CategoryGroup>& groups, const std::string& category,
                                  size_t target);
bool move_categories_between_groups(std::vector<CategoryGroup>& groups, size_t source, size_t target,
                                    const std::vector<std::string>& categories);
bool reorder_category(std::vector<CategoryGroup>& groups, size_t group, const std::string& category,
                      int direction, const std::vector<std::string>& available);
bool reorder_categories(std::vector<CategoryGroup>& groups, size_t group,
                        const std::vector<std::string>& categories, int direction,
                        const std::vector<std::string>& available);

} // namespace th
