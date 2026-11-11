#include "domain/category_groups.h"

#include <algorithm>
#include <array>
#include <unordered_set>

#include "domain/models.h"

namespace th {

namespace {

// UTF-8 escaped so MSVC doesn't depend on the build machine's code page.
const std::array<std::string, kCategoryGroupCount> kGroupNames = {
    "\xe6\x9d\x82\xe9\xa1\xb9",                                     // 杂项
    "\xe4\xb8\x9c\xe9\x83\xa8\xe7\x8e\x8b\xe5\x9b\xbd",             // 东部王国
    "\xe5\x8d\xa1\xe5\x88\xa9\xe5\xa7\x86\xe5\xa4\x9a",             // 卡利姆多
    "\xe7\x87\x83\xe7\x83\xa7\xe7\x9a\x84\xe8\xbf\x9c\xe5\xbe\x81", // 燃烧的远征
    "\xe5\xb7\xab\xe5\xa6\x96\xe7\x8e\x8b\xe4\xb9\x8b\xe6\x80\x92", // 巫妖王之怒
};

struct ZoneGroup {
    const char* name;
    int group;
};

// Ordered lists from CategoryGridOrder() in ui.au3.
const ZoneGroup kZoneGroups[] = {
    // 东部王国
    {"\xe8\x89\xbe\xe5\xb0\x94\xe6\x96\x87\xe6\xa3\xae\xe6\x9e\x97", 1},             // 艾尔文森林
    {"\xe8\xa5\xbf\xe9\x83\xa8\xe8\x8d\x92\xe9\x87\x8e", 1},                         // 西部荒野
    {"\xe8\xb5\xa4\xe8\x84\x8a\xe5\xb1\xb1", 1},                                     // 赤脊山
    {"\xe6\xb9\xbf\xe5\x9c\xb0", 1},                                                 // 湿地
    {"\xe6\x9a\xae\xe8\x89\xb2\xe6\xa3\xae\xe6\x9e\x97", 1},                         // 暮色森林
    {"\xe5\xb8\x8c\xe5\xb0\x94\xe6\x96\xaf\xe5\xb8\x83\xe8\x8e\xb1\xe5\xbe\xb7", 1}, // 希尔斯布莱德
    {"\xe9\x98\xbf\xe6\x8b\x89\xe5\xb8\x8c", 1},                                     // 阿拉希
    {"\xe5\xa5\xa5\xe7\x89\xb9\xe5\x85\xb0\xe5\x85\x8b", 1},                         // 奥特兰克
    {"\xe4\xb8\xb9\xe8\x8e\xab\xe7\xbd\x97", 1},                                     // 丹莫罗
    {"\xe6\xb4\x9b\xe5\x85\x8b\xe8\x8e\xab\xe4\xb8\xb9", 1},                         // 洛克莫丹
    {"\xe8\x8d\x92\xe8\x8a\x9c\xe4\xb9\x8b\xe5\x9c\xb0", 1},                         // 荒芜之地
    {"\xe8\x8d\x86\xe6\xa3\x98\xe8\xb0\xb7", 1},                                     // 荆棘谷
    {"\xe7\x81\xbc\xe7\x83\xad\xe5\xb3\xa1\xe8\xb0\xb7", 1},                         // 灼热峡谷
    {"\xe6\x82\xb2\xe4\xbc\xa4\xe6\xb2\xbc\xe6\xb3\xbd", 1},                         // 悲伤沼泽
    {"\xe8\xbe\x9b\xe7\x89\xb9\xe5\x85\xb0", 1},                                     // 辛特兰
    {"\xe8\xaf\x85\xe5\x92\x92\xe4\xb9\x8b\xe5\x9c\xb0", 1},                         // 诅咒之地
    {"\xe7\x87\x83\xe7\x83\xa7\xe5\xb9\xb3\xe5\x8e\x9f", 1},                         // 燃烧平原
    {"\xe8\xa5\xbf\xe7\x98\x9f\xe7\x96\xab", 1},                                     // 西瘟疫
    {"\xe4\xb8\x9c\xe7\x98\x9f\xe7\x96\xab", 1},                                     // 东瘟疫
    // 卡利姆多
    {"\xe6\xb3\xb0\xe8\xbe\xbe\xe5\xb8\x8c\xe5\xb0\x94", 2},                         // 泰达希尔
    {"\xe9\xbb\x91\xe6\xb5\xb7\xe5\xb2\xb8", 2},                                     // 黑海岸
    {"\xe7\x81\xb0\xe8\xb0\xb7", 2},                                                 // 灰谷
    {"\xe9\x97\xaa\xe5\x85\x89\xe5\xb9\xb3\xe5\x8e\x9f", 2},                         // 闪光平原
    {"\xe6\x9d\x9c\xe9\x9a\x86\xe5\xa1\x94\xe5\xb0\x94", 2},                         // 杜隆塔尔
    {"\xe8\xb4\xab\xe7\x98\xa0\xe4\xb9\x8b\xe5\x9c\xb0", 2},                         // 贫瘠之地
    {"\xe5\xb0\x98\xe6\xb3\xa5\xe6\xb2\xbc\xe6\xb3\xbd", 2},                         // 尘泥沼泽
    {"\xe5\x87\x84\xe5\x87\x89\xe4\xb9\x8b\xe5\x9c\xb0", 2},                         // 凄凉之地
    {"\xe8\x8f\xb2\xe6\x8b\x89\xe6\x96\xaf", 2},                                     // 菲拉斯
    {"\xe5\xa1\x94\xe7\xba\xb3\xe5\x88\xa9\xe6\x96\xaf", 2},                         // 塔纳利斯
    {"\xe6\x9c\x88\xe5\x85\x89\xe6\x9e\x97\xe5\x9c\xb0", 2},                         // 月光林地
    {"\xe6\xa3\x98\xe9\xbd\xbf\xe5\x9f\x8e", 2},                                     // 棘齿城
    {"\xe5\xae\x89\xe6\x88\x88\xe6\xb4\x9b", 2},                                     // 安戈洛
    {"\xe5\x86\xac\xe6\xb3\x89\xe8\xb0\xb7", 2},                                     // 冬泉谷
    {"\xe5\xb8\x8c\xe5\x88\xa9\xe8\x8b\x8f\xe6\x96\xaf", 2},                         // 希利苏斯
    // 燃烧的远征
    {"\xe5\x9c\xb0\xe7\x8b\xb1\xe7\x81\xab\xe5\x8d\x8a\xe5\xb2\x9b", 3},             // 地狱火半岛
    {"\xe8\xb5\x9e\xe5\x8a\xa0\xe6\xb2\xbc\xe6\xb3\xbd", 3},                         // 赞加沼泽
    {"\xe7\xba\xb3\xe6\xa0\xbc\xe5\x85\xb0", 3},                                     // 纳格兰
    {"\xe6\xb3\xb0\xe7\xbd\x97\xe5\x8d\xa1\xe6\xa3\xae\xe6\x9e\x97", 3},             // 泰罗卡森林
    {"\xe5\x88\x80\xe9\x94\x8b\xe5\xb1\xb1", 3},                                     // 刀锋山
    {"\xe8\x99\x9a\xe7\xa9\xba\xe9\xa3\x8e\xe6\x9a\xb4", 3},                         // 虚空风暴
    {"\xe5\xbd\xb1\xe6\x9c\x88\xe8\xb0\xb7", 3},                                     // 影月谷
    // 巫妖王之怒
    {"\xe6\xad\xbb\xe4\xba\xa1\xe9\xaa\x91\xe5\xa3\xab", 4},                         // 死亡骑士
    {"\xe7\xa7\x98\xe8\xa1\x80\xe5\xb2\x9b", 4},                                     // 秘血岛
    {"\xe7\xa7\x98\xe8\x93\x9d\xe5\xb2\x9b", 4},                                     // 秘蓝岛
    {"\xe5\x9a\x8e\xe9\xa3\x8e\xe5\xb3\xa1\xe6\xb9\xbe", 4},                         // 嚎风峡湾
    {"\xe5\x8c\x97\xe9\xa3\x8e\xe8\x8b\x94\xe5\x8e\x9f", 4},                         // 北风苔原
    {"\xe9\xbe\x99\xe9\xaa\xa8\xe8\x8d\x92\xe9\x87\x8e", 4},                         // 龙骨荒野
    {"\xe6\x99\xb6\xe6\xad\x8c\xe6\xa3\xae\xe6\x9e\x97", 4},                         // 晶歌森林
    {"\xe7\xa5\x96\xe8\xbe\xbe\xe5\x85\x8b", 4},                                     // 祖达克
    {"\xe7\xb4\xa2\xe6\x8b\x89\xe6\x9f\xa5\xe7\x9b\x86\xe5\x9c\xb0", 4},             // 索拉查盆地
    {"\xe7\x81\xb0\xe7\x86\x8a\xe4\xb8\x98\xe9\x99\xb5", 4},                         // 灰熊丘陵
    {"\xe9\xa3\x8e\xe6\x9a\xb4\xe5\xb3\xad\xe5\xa3\x81", 4},                         // 风暴峭壁
    {"\xe5\x86\xb0\xe5\x86\xa0\xe5\x86\xb0\xe5\xb7\x9d", 4},                         // 冰冠冰川
};

} // namespace

const std::string& category_group_name(int group) {
    static const std::string empty;
    if (group < 0 || group >= kCategoryGroupCount) return empty;
    return kGroupNames[static_cast<size_t>(group)];
}

int category_group_of(const std::string& category) {
    for (const auto& zone : kZoneGroups) {
        if (category == zone.name) return zone.group;
    }
    return 0;
}

int category_order(const std::string& category) {
    for (size_t i = 0; i < std::size(kZoneGroups); ++i) {
        if (category == kZoneGroups[i].name) return static_cast<int>(i);
    }
    return -1;
}

std::vector<CategoryGroup> group_categories(const std::vector<std::string>& categories) {
    std::vector<CategoryGroup> groups;
    groups.reserve(kCategoryGroupCount);
    for (int g = 0; g < kCategoryGroupCount; ++g) {
        groups.push_back(CategoryGroup{category_group_name(g), {}});
    }
    groups[0].categories.push_back(ALL_CATEGORY);
    for (const auto& category : categories) {
        if (category == ALL_CATEGORY) continue;
        if (category_group_of(category) == 0) groups[0].categories.push_back(category);
    }
    for (const auto& zone : kZoneGroups) {
        if (std::find(categories.begin(), categories.end(), zone.name) != categories.end()) {
            groups[static_cast<size_t>(zone.group)].categories.emplace_back(zone.name);
        }
    }
    return groups;
}

bool valid_category_group_name(const std::string& name) {
    if (name.empty() || name.size() > 127) return false;
    bool non_space = false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return false;
        if (c != ' ') non_space = true;
    }
    return non_space;
}

bool valid_category_group_config(const std::vector<CategoryGroup>& groups) {
    if (groups.empty()) return false;
    std::unordered_set<std::string> names, categories;
    for (const auto& group : groups) {
        if (!valid_category_group_name(group.name) || !names.insert(group.name).second) return false;
        for (const auto& category : group.categories)
            if (category == ALL_CATEGORY || !categories.insert(category).second) return false;
    }
    return true;
}

std::vector<CategoryGroup> editable_category_groups(const std::vector<std::string>& categories,
                                                     const std::vector<CategoryGroup>& saved,
                                                     bool customized) {
    if (!customized || !valid_category_group_config(saved)) {
        std::vector<std::string> all;
        std::unordered_set<std::string> seen;
        for (const auto& category : categories)
            if (category != ALL_CATEGORY && seen.insert(category).second) all.push_back(category);
        for (const auto& zone : kZoneGroups)
            if (seen.insert(zone.name).second) all.emplace_back(zone.name);
        auto result = group_categories(all);
        for (auto& group : result)
            group.categories.erase(std::remove(group.categories.begin(), group.categories.end(), ALL_CATEGORY),
                                   group.categories.end());
        return result;
    }
    auto result = saved;
    std::unordered_set<std::string> assigned;
    for (const auto& group : result)
        assigned.insert(group.categories.begin(), group.categories.end());
    for (const auto& category : categories) {
        if (category == ALL_CATEGORY || !assigned.insert(category).second) continue;
        const std::string& preferred = category_group_name(category_group_of(category));
        const auto group = std::find_if(result.begin(), result.end(), [&](const CategoryGroup& g) {
            return g.name == preferred;
        });
        (group == result.end() ? result.front() : *group).categories.push_back(category);
    }
    return result;
}

std::vector<CategoryGroup> navigation_category_groups(const std::vector<std::string>& categories,
                                                       const std::vector<CategoryGroup>& saved,
                                                       bool customized) {
    if (!customized || !valid_category_group_config(saved)) return group_categories(categories);
    auto result = editable_category_groups(categories, saved, true);
    const std::unordered_set<std::string> available(categories.begin(), categories.end());
    for (auto& group : result) {
        group.categories.erase(std::remove_if(group.categories.begin(), group.categories.end(),
            [&](const std::string& name) { return available.find(name) == available.end(); }), group.categories.end());
    }
    result.front().categories.insert(result.front().categories.begin(), ALL_CATEGORY);
    return result;
}

bool add_category_group(std::vector<CategoryGroup>& groups, const std::string& name) {
    if (!valid_category_group_name(name) || std::any_of(groups.begin(), groups.end(),
        [&](const CategoryGroup& group) { return group.name == name; })) return false;
    groups.push_back({name, {}});
    return true;
}

bool rename_category_group(std::vector<CategoryGroup>& groups, size_t index, const std::string& name) {
    if (index >= groups.size() || !valid_category_group_name(name)) return false;
    for (size_t i = 0; i < groups.size(); ++i)
        if (i != index && groups[i].name == name) return false;
    groups[index].name = name;
    return true;
}

bool delete_category_group(std::vector<CategoryGroup>& groups, size_t index) {
    if (groups.size() <= 1 || index >= groups.size()) return false;
    auto categories = std::move(groups[index].categories);
    groups.erase(groups.begin() + index);
    groups.front().categories.insert(groups.front().categories.end(), categories.begin(), categories.end());
    return true;
}

bool move_category_group(std::vector<CategoryGroup>& groups, size_t from, size_t to) {
    if (from >= groups.size() || to >= groups.size() || from == to) return false;
    auto group = std::move(groups[from]);
    groups.erase(groups.begin() + from);
    groups.insert(groups.begin() + to, std::move(group));
    return true;
}

bool move_category_between_groups(std::vector<CategoryGroup>& groups, const std::string& category, size_t target) {
    if (target >= groups.size() || category == ALL_CATEGORY) return false;
    for (size_t i = 0; i < groups.size(); ++i) {
        auto it = std::find(groups[i].categories.begin(), groups[i].categories.end(), category);
        if (it == groups[i].categories.end()) continue;
        return move_categories_between_groups(groups, i, target, {category});
    }
    return false;
}

bool move_categories_between_groups(std::vector<CategoryGroup>& groups, size_t source, size_t target,
                                    const std::vector<std::string>& categories) {
    if (source >= groups.size() || target >= groups.size() || source == target || categories.empty()) return false;
    const std::unordered_set<std::string> wanted(categories.begin(), categories.end());
    if (wanted.count(ALL_CATEGORY) || wanted.size() != categories.size()) return false;
    auto& from = groups[source].categories;
    std::vector<std::string> moving;
    for (const auto& category : from) if (wanted.count(category)) moving.push_back(category);
    if (moving.size() != wanted.size()) return false;
    from.erase(std::remove_if(from.begin(), from.end(), [&](const std::string& value) {
        return wanted.count(value) != 0;
    }), from.end());
    auto& to = groups[target].categories;
    to.insert(to.end(), moving.begin(), moving.end());
    return true;
}

bool reorder_category(std::vector<CategoryGroup>& groups, size_t group, const std::string& category,
                      int direction, const std::vector<std::string>& available) {
    return reorder_categories(groups, group, {category}, direction, available);
}

bool reorder_categories(std::vector<CategoryGroup>& groups, size_t group,
                        const std::vector<std::string>& categories, int direction,
                        const std::vector<std::string>& available) {
    if (group >= groups.size() || (direction != -1 && direction != 1) || categories.empty()) return false;
    const std::unordered_set<std::string> wanted(categories.begin(), categories.end());
    if (wanted.size() != categories.size() || wanted.count(ALL_CATEGORY)) return false;
    const std::unordered_set<std::string> visible(available.begin(), available.end());
    auto& list = groups[group].categories;
    std::vector<size_t> positions;
    size_t matched = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        if (visible.count(list[i])) {
            positions.push_back(i);
            if (wanted.count(list[i])) ++matched;
        }
    }
    if (matched != wanted.size()) return false;
    bool changed = false;
    if (direction == -1) {
        for (size_t i = 1; i < positions.size(); ++i) {
            if (wanted.count(list[positions[i]]) && !wanted.count(list[positions[i - 1]])) {
                std::swap(list[positions[i]], list[positions[i - 1]]);
                changed = true;
            }
        }
    } else {
        for (size_t i = positions.size(); i > 1; --i) {
            if (wanted.count(list[positions[i - 2]]) && !wanted.count(list[positions[i - 1]])) {
                std::swap(list[positions[i - 2]], list[positions[i - 1]]);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace th
