# AGENTS.md — TeleportHackDX 项目记忆

本文件是为了在新会话中快速接续本项目而写的压缩上下文备忘。在本仓库做任何事情之前，请先读这份文件。

## 交互规则（继承自 ~/AGENTS.md，在本仓库同样要遵守）

每轮助手回复结束时，必须且只能调用一次 `ask_user`。
- 提供 2-4 个具体选项，同时允许自由文本输入。
- 问题必须直接针对当前任务。
- 如果 `ask_user` 可用，不要以纯文本结束本轮。
- 如果 `ask_user` 不可用，需明确说明这一点。

## 这个项目是什么

`../TeleportHackOnVanilla`（一个用于 WoW 1.12.1/1.12.3/3.3.5 的 AutoIt 传送外挂工具）的
C++/ImGui 移植版，需求是让界面**运行在游戏内部**，通过 DirectX9 Hook 实现，而不是外部的
AutoIt/Python 进程。参考的 Hook 方案：
https://github.com/AzDeltaQQ/WoW3.3.5aDirectXHookWithGui。

早期和用户一起确定的范围决策（依然有效，除非用户另有要求，否则不要重新讨论）：
- **只做核心传送外挂功能。** 故意排除的部分：AutoIt 的 `bot/` 脚本
  （硬编码账号密码的自动登录、副本刷本机器人）以及多开客户端的"sync-tp"
  —— 这些都没有被要求实现，而且登录脚本里嵌入了真实的账号密码。
- **手动选择版本下拉框**（1.12.1 / 1.12.3 / 3.3.5），不做自动检测。
- 所有代码放在一个**全新的独立仓库**里：
  `https://github.com/heshanu3d/TeleportHackDX`（公开仓库），与
  `TeleportHackOnVanilla` 是兄弟目录关系，不嵌套在它内部。

## 仓库 / CI / 推送机制（重要，容易忽略的细节）

- 远程仓库：`git@github.com:heshanu3d/TeleportHackDX.git`，分支 `master`。
- **推送走 SSH**，使用沙盒里已经存在且已授权的 key：`~/.ssh/id_ed25519_github`
  （已用 `ssh -T git@github.com` 验证过，返回 "Hi heshanu3d!"）。仓库本地的 git
  配置已经设置好 `core.sshCommand` 来使用这把 key
  （`ssh -o IdentitiesOnly=yes -i ~/.ssh/id_ed25519_github`），所以直接
  `git push` 就能用 —— 推送不需要 token。
- **读取 Actions 运行日志**需要一个*另外的* GitHub API token（SSH 只能做 git 操作，
  不能调 REST API）。用户在本次会话早些时候生成过一个短期有效的细粒度 PAT
  （Actions: Read-only 权限，只作用于这一个仓库），让我能通过
  `curl -H "Authorization: Bearer $TOKEN" https://api.github.com/...`
  拉取失败构建的日志。那个 token **没有**保存在仓库里的任何地方（这是对的），
  而且很可能已经过期 / 不应假定它还有效 —— 如果之后还需要读日志，
  请向用户要一个新的（同样的做法：细粒度 PAT，Actions: Read-only，
  scope 限定到 `heshanu3d/TeleportHackDX`），或者请用户直接从 Actions 网页
  UI 里把日志文本贴过来。
- CI：`.github/workflows/build.yml`，运行在 `windows-latest`，通过 CMake 以
  `-A Win32` 构建 Release，上传一个**扁平的** `dist/` 文件夹
  （dll + exe + favlist.fav + hotkey.txt 都在 zip 根目录，没有
  `build/bin/Release/` 这层嵌套），命名为
  `TeleportHackDX-<VERSION>-g<shortsha>-win32`。仓库根目录的 `VERSION`
  文件是唯一的版本号来源，CMake（`project(... VERSION ...)`）和 workflow
  都从这里读取。
- 截至 commit `610d4c1`（最后一次验证通过的推送），CI 是绿的，我们自己的代码在
  `/W4 /permissive-` 下**零警告**（在我把这些编译选项限定到只作用于我们自己的
  target 之前，vendored 的 MinHook 代码有一些无害的警告）。

## 验证方法（本沙盒里没有 Windows/MSVC）

每次改动都反复用到以下两种手段：

1. **对纯逻辑层做真实编译+运行**（不涉及任何 Windows 头文件）：领域模型、
   `FavlistRepository`、`FavouritesService`、`FavouritesPorter`、
   `HotkeyConfigRepository`、`SettingsRepository`、`FeatureService`
   （它只接触 `MemoryBackend` 接口，不直接调用 WinAPI）。用普通的
   `g++ -std=c++17` 针对一个临时写的 `main()` 编译，用真实的 UTF-8/中文测试数据
   跑一遍逻辑。这个手段能便宜地抓到真正的 bug，是性价比最高的检查方式 ——
   **改动这些文件时一定要做这一步。**
2. **对 Windows/D3D9/ImGui 相关文件做纯语法检查**（`hook.cpp`、`dllmain.cpp`、
   `app.cpp`、`desktop/main.cpp`、`teleport_service.cpp`、
   `inprocess_backend.cpp`、`outofprocess_backend.cpp`、`process_list.cpp`）：
   手写一套最简化的 `Windows.h`/`d3d9.h`/`MinHook.h`/`imgui_impl_*.h` 桩文件放在
   一个目录里，然后跑
   `clang++ -std=c++17 -fsyntax-only -I<shim> -I src ...`。这**不是**真正的编译，
   但确实提前抓到过真 bug（比如一个 `WM_CLOSE`/匿名命名空间导致的链接问题、
   一个在句柄已销毁后调用 `GetWindowRect` 的问题）。但如果桩文件本身写错了，
   也可能带来**虚假的安心感**（比如我伪造的 `imgui_impl_win32.h` 曾经声明了一个
   *真实*头文件里故意省略的函数 —— CI 抓到了这个 MSVC 错误，而 Linux 上的桩文件
   却没抓到）。**把 clang 桩文件检查"通过"当作必要但不充分的条件；Windows 上的
   CI 才是最终的事实依据。** 每次改动都要真正推送并轮询 Actions 跑完，才能算这个
   改动完成，就算改动"明显正确"也不要跳过这一步。
3. 相关工作都在 `/tmp/opencode/<scratch-dir>` 里做，做完就删掉；
   绝不要把桩文件留在正式仓库里。

## 架构（src/ 目录结构）

```
src/
  dllmain.cpp / core/hook.cpp   仅 DLL 使用：Hook IDirect3DDevice9 的
                                 EndScene/Reset（用 MinHook）、WndProc hook、
                                 Win+Insert 切换显示、加载中文字体、持有 th::App
  desktop/main.cpp               仅 EXE 使用：普通 Win32 窗口 + 自己的 D3D9
                                 设备（改编自 Dear ImGui 的
                                 example_win32_directx9），同样用 th::App
  domain/                        Position/TeleportPoint/Category/
                                 HotkeyBinding + 各版本 GameVersion 内存偏移表
                                 （1.12.1/1.12.3/3.3.5），逐字从
                                 TeleportHackOnVanilla/win/.../versions.py 移植
  memory/                        MemoryBackend 接口；InProcessBackend
                                 （SEH 保护的直接指针解引用，DLL 用）和
                                 OutOfProcessBackend（OpenProcess +
                                 ReadProcessMemory/WriteProcessMemory，
                                 桌面 attach 模式用）
  services/                      TeleportService（指针链解析、坐标轴交换的
                                 各种奇怪规则、分步传送 —— 既有阻塞版本，
                                 也有每帧驱动一次的非阻塞
                                 TeleportStepSession），FeatureService
                                 （AntiJump/AutoLoot/PatchLoot/LuaUnlock
                                 字节补丁开关 + 3.3.5 加速外挂），
                                 FavouritesService + FavouritesPorter
                                 （增删改查/导入导出/合并去重），
                                 HotkeyService（Win32 的 RegisterHotKey，
                                 不是后台线程）
  repository/                    favlist.fav / hotkey.txt / settings.json /
                                 Teleport.lua 的读写器
  ui/app.{h,cpp}                 th::App —— 唯一的共享类。持有所有 service，
                                 每帧渲染整个 ImGui 界面。通过
                                 BackendMode::{InProcess, Attach} 被两个可执行
                                 文件**原样**共用。任何界面或 favlist/hotkey
                                 逻辑的改动都应该只改这一处，同时对两种构建生效。
  util/                          paths.cpp（把 settings/favlist/hotkey 路径
                                 解析为相对于 DLL/EXE 自身所在目录，而不是
                                 cwd —— 通过对自己地址调用
                                 GetModuleHandleExA 实现），process_list.cpp
                                 （Toolhelp32 枚举进程 + 玩家名预览，
                                 仅桌面 attach 模式使用）
```

CMake：`th_core` 这个 STATIC 库包含除了 `dllmain.cpp`/`core/hook.cpp`
（仅 DLL）和 `desktop/main.cpp`（仅 EXE）之外的所有代码。`TeleportHackDX`
（SHARED）和 `TeleportHackDesktop`（可执行文件）都链接 `th_core`。`th_core`
以 PUBLIC 方式链接 `imgui` + `comdlg32`（它们的符号只有在最终的 DLL/EXE
链接进来之后才能真正解析）。`minhook`/`d3d9` 直接链接在两个最终 target 上，
不经过 `th_core`。

## 已经落实的一些不明显的设计要点

- **非阻塞式分步传送。** 原始 AutoIt/Python 版本每一步之间的 `Sleep()`
  会冻结*整个游戏*，因为现在我们运行在游戏自己的渲染线程上。
  `TeleportStepSession::tick()` 改为基于 `QueryPerformanceCounter` 的计时，
  每渲染一帧推进一次，由 `App::tick()` 每帧调用，不管悬浮界面是否可见。
- **热键通过 `RegisterHotKey`/`WM_HOTKEY` 实现**，不是后台线程 ——
  即使悬浮界面被隐藏也能生效，两个构建都是通过被 hook/持有的 `WndProc`
  转发到 `App::on_wndproc()`。
- **窗口位置/大小持久化**（`settings.json` 里的
  `Settings::window_x/y/width/height`）：在 `BackendMode::InProcess`
  模式下，这几个字段控制悬浮的 ImGui 面板（每帧在 `Begin()` 之后捕获当前值，
  通过 `ImGuiCond_FirstUseEver` 恢复）；在桌面版里则控制*真正的操作系统窗口*。
  已经修过一个 bug：不要在消息循环处理完 `WM_QUIT` 之后再调用
  `GetWindowRect(hwnd,...)` —— 那时候 `DestroyWindow` 早就已经让句柄失效了。
  改为通过 `WM_MOVE`/`WM_SIZE` 持续把矩形记录到一个全局变量里，退出时用
  最后一次记录到的值（见 `src/desktop/main.cpp`）。
- **所有地址都假定镜像加载在默认（未开 ASLR）基址** —— 和原始 AutoIt/Python
  工具的假设一致；没有实现重定位逻辑，而且是刻意不做的（这里不要加不必要的
  复杂度）。
- Favlist/hotkey 文本是真正的 UTF-8（已经用 `favlist.fav` 里真实的中文数据
  验证过）—— 任何地方都不需要 GBK/ANSI 转换。

## 截至 commit `610d4c1`（已推送，CI 绿）的状态 —— 以下内容都已完成并验证过

1. 完整移植了核心传送外挂功能（收藏点增删改查/分类/搜索、即时传送 + 分步传送、
   4 个字节补丁开关、加速外挂、导入/导出/合并、热键、设置）。
2. `TeleportHackDX.dll`（注入用）和 `TeleportHackDesktop.exe`（独立桌面版，
   通过跨进程内存读写 attach）都原样共用 `th::App`。
3. CI 在 `windows-latest` 上是绿的，零警告，带版本号且扁平化的产物打包。
4. 两种构建的窗口位置/大小持久化（已修复过 bug）。
5. `README.md` + `使用说明.html`（中文使用指南：构建、Cheat Engine 注入
   分步教程、桌面 attach 模式用法、故障排查）已经更新到这一步为止的全部内容。
6. 用户已经手动把 DLL 注入到真实的 WoW 客户端，确认悬浮界面能显示、
   传送功能端到端可用。

## ✅ 已完成 —— 朝向（facing）支持

这项功能曾在本文件更早的版本里被记录为"待办"（因为上一次会话的对话记录里
*叙述*了一批 patch，但实际上从未真正写入磁盘 —— 当时用 `grep`/`git status`
验证过）。现在已经真正实现、验证并推送。以下是改动内容和原因的总结，
以便之后需要重新推导或扩展这部分行为时参考：

- `domain/models.h`：`Position` 新增了 `std::optional<double> orientation`
  （单位：弧度）。缺省（missing）表示"保持当前朝向不变"；这和显式的 `0`
  是两回事，这个区别贯穿了下面提到的解析、序列化、合并去重、内存写入的
  每一处。
- `repository/favlist_repository.{h,cpp}`：`load()` 同时接受旧的 4 字段行
  （`desc#x#y#z`，无朝向）、新的 5 字段行（`desc#x#y#z#r`，`r` 可以为空），
  以及旧版 4 行和 5 行两种多行记录格式（用一个启发式规则去"偷看"可能存在的
  第 5 行：如果它是空的或者是数字，就认为属于当前记录并消耗掉；否则就认为
  它是下一条记录的描述，此时只消耗 4 行）。格式错误的朝向字段会跳过这一整条
  记录（和已有的"遇到坏数字就静默跳过"的行为保持一致），而不是报错。
  `save()` 总是输出规范化的 5 字段行（未设置朝向时，末尾留一个空字段）。
- `services/teleport_service.{h,cpp}`：`read_position()` 只在
  `version_.pos_r != 0` 时（也就是只有 3.3.5）才读取 `pos_r`；
  `write_position()` 只有在客户端支持*并且* `pos.orientation.has_value()`
  时才写入 `pos_r`。`teleport_step()` 和 `TeleportStepSession` 只在
  最后一步应用目标朝向 —— 不是每一个中间步骤都应用，这样平滑走位的过程中
  角色不会一直转来转去，只有走到终点时才"咔"一下转向目标方向。
- `services/favourites_porter.cpp`：合并去重用的签名元组现在也包含
  `orientation`，所以同一坐标下"无朝向 / 朝向为 0 / 朝向非 0"会被当作
  不同的记录（和已有的"描述文本也算进签名里"的设计理念一致）。
- `ui/app.cpp`：收藏点表格新增了第 5 列"r"（未设置时显示占位符 `-`）；
  传送日志行在有朝向值时会带上 `r=...`。
- `tests/orientation_tests.cpp`（新增的 `orientation_tests` CTest target，
  接入 `.github/workflows/build.yml`，在 Release 构建步骤之后、产物打包
  之前，通过 `ctest --test-dir build -C Release --output-on-failure` 运行）：
  覆盖了新旧格式以及旧版多行 favlist 格式、"缺省 vs 零 vs 非零"这个区别
  的端到端验证、`save()` 的往返规范化、按朝向区分的合并去重、基于
  `FakeBackend` 对 3.3.5 的 `pos_r` "只在有值时才读写"这一行为的测试，
  并且确认 1.12.1/1.12.3 永远不会碰 `pos_r`（由这两个 profile 的
  `version_.pos_r == 0` 保护）。
- 用户贴的那些示例行（包括 `#...#0` 和末尾空 `#` 这两种情况）被原样用作
  测试用的固定数据，能正确解析。
- `README.md` 和 `使用说明.html`（第 6 节的表格说明、第 7 节的文件格式）
  已更新，记录了新字段/新列。

**这次真正执行过的验证**（参见上面的"验证方法"一节，说明为什么用两种手段）：
1. 用真实的 `g++ -std=c++17 -Wall -Wextra` 编译+运行了纯逻辑层
   （`models.cpp`、`favlist_repository.cpp`、`favourites_service.cpp`、
   `favourites_porter.cpp`），针对一个临时写的测试用户给出的原样示例数据 ——
   所有断言都通过。
2. 对本仓库里真实的 1662 行 `favlist.fav` 做了回归测试：加载后完全不变
   （0 个点带朝向，符合"这是一份全旧格式的文件"的预期），并且能稳定地
   经过保存/重新加载的往返。
3. `tests/orientation_tests.cpp` 本身：用真实的 g++ 编译+链接+运行（借助
   一个临时的 `Sleep`/`QueryPerformanceCounter` 桩文件，好让
   `teleport_service.cpp` 能在 Linux 上链接） —— 所有断言都通过，
   包括基于 `FakeBackend` 对 3.3.5 的 `pos_r` 行为的测试。
4. 对改动到的 Windows 相关文件（`teleport_service.cpp`、`ui/app.cpp`）
   做了 `clang++ -fsyntax-only` 桩文件检查 —— 干净无误。
5. 在 Linux 上（原生编译器，不是真正的 Win32 构建）做了
   `cmake -S . -B <dir>` 仅配置阶段的检查，验证加入 `orientation_tests`
   target 之后 `CMakeLists.txt` 语法正确 —— 配置成功。
6. 推送后轮询了真实的 `windows-latest` CI 直到跑完 —— 具体是哪次
   run/commit 见提交记录；如果这一行没有被更新为具体结果，就应该把
   CI 状态当作**这项改动还没有被确认为绿色**，去
   https://github.com/heshanu3d/TeleportHackDX/actions 核实之后再相信它。

**已确认为绿**：commit `29695e9`，CI run `30008276468` —— `Configure`、
`Build (Release)`、`Test (Release)`（也就是 `ctest` 在 `windows-latest`
上真正跑了 `orientation_tests`，不是 Linux 上的近似模拟）全部成功，
产物 `TeleportHackDX-0.1.0-g29695e9-win32` 已上传。

## ✅ 已完成 —— "读取到炉石"：把当前坐标同步进服务端的 Teleport.lua

用户在自己的 WoW 3.3.5 服务端上跑了一个独立的 Eluna/mod-lua 脚本
`~/code/work/0722_DirectXHookWithGui/Teleport.lua`（一个"超级炉石"道具的
gossip 菜单），它的坐标存放在一张
`local FAV = { {"name", mapId, x, y, z, o}, ... }` 表里。这张表通常是由
一个配套的 `gen_fav.py`（不在本仓库里，和 `Teleport.lua` 放在一起）
从 `favlist.fav` **整体重新生成**的 —— 按行一一对应，mapId 通过名字里的
关键词自动推断。

我加了一个**外科手术式的单条目编辑器**，这样做快速的坐标微调时就不用
每次都整体重新生成了：

- `src/repository/teleport_lua_repository.{h,cpp}`（新增）：
  `update_entry(description, pos, error)` 找到 `local FAV = { ... }`
  这张表，定位到*解码后*的 Lua 字符串名字与 `description` 完全相等的那一行，
  只重写这一行的 x/y/z（总是重写）和 o（只有当 `pos.orientation.has_value()`
  时才重写 —— 否则这一行原有的 o 这个 token 会原样保留，和 favlist.fav
  里"缺省即保持当前朝向"的约定一致）。这一行的名字和 mapId 这两个 token
  会被逐字节原样复制，不会重新编码。文件里的其它每一行 —— 其它坐标行、
  菜单代码、注释 —— 都不会被改动。文件的 UTF-8 BOM 和 CRLF 换行符会被
  检测出来并在写入时保留。
- `ui/app.cpp`：在增删改查那一行里，"编辑"/"删除"旁边新增了一个
  "读取到炉石"按钮。它像"编辑"一样读取当前坐标
  （`teleport_->read_position()`），然后通过
  `current_point()->description`（当前选中行*存储的*描述文本 ——
  刻意不用 `desc_buf_`，因为那是一个可以自由编辑的文本框，其内容不一定
  能在 Teleport.lua 里找到对应条目）去查找要修改的条目。
- `Settings::teleport_lua_path`（新增，默认是空字符串 —— 大多数用户的
  部署环境里根本没有这个文件放在 DLL/EXE 旁边）：可以在 Settings 弹窗里
  和已有的 favlist.fav/hotkey.txt 路径字段一起编辑。
- `tests/teleport_lua_repository_tests.cpp`（新增的 CTest target）：
  一个带 BOM+CRLF 的合成测试数据，覆盖了正常更新、名字里带转义引号的情况、
  缺省朝向时保留原有 o 值、名字找不到/文件不存在/找不到 FAV 表这几种
  错误路径。

**验证方式**：用真实的 g++ 编译+链接+运行了这套合成测试数据（全部通过），
另外（没有作为测试提交，因为需要用到真实文件，而它不属于本仓库）针对
真实的、212588 字节/3319 行的 `Teleport.lua` 做了一次人工检查：一次
空操作式的更新（原样写回已有的 x/y/z，不改朝向）产生了一份**逐字节完全
相同**的文件；一次真实的数值改动正确地只修改了目标那一行，同时保持了
完全相同的行数。之后推送并确认在 `windows-latest` 上是绿的：commit
`1fee532`，CI run `30012301908` —— `Test (Release)` 真正跑了
`orientation_tests` 和新增的 `teleport_lua_tests` 两者，都通过了，
产物 `TeleportHackDX-0.1.0-g1fee532-win32` 已上传。

如果之后需要再次验证针对真实文件的往返一致性，不要把它做成提交到仓库里的
测试（真实文件不属于本仓库，路径也是机器相关的）—— 直接照着上面描述的
临时验证方式来做：把真实文件复制到一个临时路径，用完全相同的现有数值
调用一次 `update_entry()`，然后和原文件做逐字节 diff。

## 用户提出但尚未处理的请求

- 截至本文撰写时没有遗留事项。用户之前另外提过想实际重新测试一下窗口
  位置/大小的持久化效果（之前只是靠推理 + 桩文件检查 + CI 绿色来验证的，
  从来没有真人上手确认过）—— 如果之后再提起，值得回头补上这一步。
