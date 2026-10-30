#include "domain/category_groups.h"

#include <array>

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

// Exactly the Switch/Case lists of CategoryGridGroup() in ui.au3.
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
    {"\xe8\xbe\xbe\xe7\xba\xb3\xe8\x8b\x8f\xe6\x96\xaf", 2},                         // 达纳苏斯
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

std::vector<CategoryGroup> group_categories(const std::vector<std::string>& categories) {
    std::vector<CategoryGroup> groups;
    groups.reserve(kCategoryGroupCount);
    for (int g = 0; g < kCategoryGroupCount; ++g) {
        groups.push_back(CategoryGroup{category_group_name(g), {}});
    }
    groups[0].categories.push_back(ALL_CATEGORY);
    for (const auto& category : categories) {
        if (category == ALL_CATEGORY) continue;
        groups[static_cast<size_t>(category_group_of(category))].categories.push_back(category);
    }
    return groups;
}

} // namespace th
