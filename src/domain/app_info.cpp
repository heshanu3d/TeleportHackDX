// NOTE: this file contains UTF-8 Chinese string literals on purpose and is
// compiled with /utf-8 under MSVC (see CMakeLists.txt); GCC/Clang already
// treat sources and narrow literals as UTF-8.
#include "domain/app_info.h"

#ifndef TH_BUILD_VERSION
#define TH_BUILD_VERSION "dev"
#endif

namespace th {

const std::string& build_version() {
    static const std::string version = TH_BUILD_VERSION;
    return version;
}

const std::string& app_author() {
    static const std::string author = "shan.he";
    return author;
}

const std::string& release_notes() {
    static const std::string notes =
        "0.5.5\n"
        "  - 分类设置支持 Ctrl 和 Shift 多选小类，可批量移动和调整顺序。\n"
        "  - 新增分类设置页，可管理分类导航的大类、小类归属和顺序。\n"
        "  - 新增右键传送开关，可右键收藏坐标进行传送。\n"
        "  - 支持按分类独立记住搜索内容，并保存传送 API 开关状态。\n"
        "  - 搜索历史支持持久化、下拉选择和设置页增删改；分类由分类导航选择。\n"
        "  - 注入版设置页支持配置折叠 / 展开窗口快捷键。\n"
        "  - 设置页支持界面透明度、主窗口与坐标列表高度，以及坐标列显示。\n"
        "  - 新增及时保存选项，最近使用的坐标移至操作按钮下方。\n"
        "  - 调整坐标操作按钮的排列和字号，设置页可调整按钮宽度、高度和间隔。\n"
        "  - 传送 API 仅用于跨地图，同地图继续使用原传送方式；设置页支持亮色和暗黑主题。\n"
        "  - 同步 TeleportHackOnVanilla v0.5.4：分类导航按地图预设顺序排列，更新收藏地图 ID。\n"
        "  - 1.12.3 注入版新增收藏双击传送 API，需服务端启用配套协议。\n"
        "\n"
        "0.1.3\n"
        "  - 同步 TeleportHackOnVanilla：favlist 增加可选 mapid 字段（描述#x#y#z#r#mapid）。\n"
        "  - 1.12.1 / 1.12.3 读取地图与朝向，传送时写入朝向；目标地图与当前地图不同时取消传送。\n"
        "  - 修正 1.12.3 读取当前坐标时 x/y 顺序与 AutoIt 版不一致。\n"
        "  - 增加分类导航、最近使用的坐标（20 条，双击再次传送）、设置和关于页。\n"
        "  - 桌面版只有一个 WoW 进程时自动连接，并按设置的间隔检测 PID 变化。\n"
        "  - 增加 WinForms / WPF 桌面界面和 C ABI（TeleportHackCApi.dll）。\n"
        "  - 增加 TeleportHackLauncher.exe、3.3.5 移动速度、桌面置顶、Teleport.lua 动态锚点。\n"
        "\n"
        "0.1.2\n"
        "  - 注入版中文输入法与 WoW 隔离，修复聊天输入乱码。\n"
        "  - 增加「插后 表&炉石」，同时插入收藏表和 Teleport.lua。\n"
        "  - 注入版 / 桌面版 ImGui 布局分别保存到 settings.json。\n"
        "  - CI 产物按最近 tag 命名，GitHub Actions 升级到 Node 24。\n"
        "\n"
        "0.1.1\n"
        "  - 首个版本：DirectX9 注入版 + 桌面 Attach 版，移植传送、分步传送、收藏增删改查、\n"
        "    导入导出、补丁开关、加速和热键。\n"
        "  - 支持 3.3.5 朝向和「读取到炉石」（Teleport.lua）。\n"
        "  - 修正 3.3.5 传送写入 X/Y 顺序；保存窗口位置和大小。\n";
    return notes;
}

} // namespace th
