# 联系人相关对话框对照 tauri-qqnt 修复计划

## 背景

前一轮把 AddFriend / FriendManager / GlobalSearch / CreateGroup 四个对话框接了后端并做了初步 QQNT 重排，但和 `D:\C++VS pro\tauri-qqnt` 的 React 参考实现比对后，用户指出仍有明显差距：

1. **好友管理器**：没有「增加分组 / 删除分组」，右上角「X 关闭」按钮没显示出来。
2. **＋号按钮**：样式没调好（尺寸/图标/下拉不对）。
3. **加好友**：样式没调好。
4. **创建群聊**：整个步骤流程和参考对不上（参考是「选成员 / 按分类创建 / 填信息」的分叉流程，当前是简单三步线性向导）。

唯一视觉与交互对照物：`D:\C++VS pro\tauri-qqnt\src\components\contact\*.tsx`。保持现有后端/协议不变，只改 UI 层。

## 关键约束（务必先读）

- **好友分组是纯本地概念**：`ContactDisplayData` 有 `group` 字段，但 `Client`/协议层**没有**任何好友分组命令。参考实现同样只用本地 `group` 字段 + 预设分组列表（`DEFAULT_CONTACT_GROUPS = ['我的好友','朋友','家人','同学','公会的人']`），无服务器同步。
- **参考里「添加分组」本身是 disabled 占位**（`title="即将上线"`），**且没有「删除分组」**。所以严格对照参考 = 添加分组做成禁用占位、无删除。用户要「增加/删除分组」属于**超出参考**的诉求 —— 见步骤 1 的两个选项，需用户拍板。
- Qt 端有三套构建；验证以 CMake `build-tests-qt`（Qt mingw1310 工具链）为准。Qt Creator 的 `bin/` 构建改 `.pro` 后要重跑 qmake，且 PATH 里 msys2 的 ld 会导致 ld-116，需把 `D:\Qt\Tools\mingw1310_64\bin` 提前。
- Read 工具在本仓库常有显示乱码/错行，判断真实内容只信 `grep` 和编译器报错，改完立即编译。

## 逐项差距与修复方案

### 1. 好友管理器（FriendManagerDialog）分组 + X 按钮

**参考（FriendManagerModal.tsx）目标：**
- 整体是无标题栏的浮层：`840x600`，左 200px 分组栏 + 右主区。
- 左分组栏：顶部「全部好友」行（带总数）；下方「分组」小标题 + 分组列表（每项显示名称 + 人数）；底部「＋ 添加分组」按钮（**参考中 disabled，即将上线**）。
- 右主区：标题「好友管理器」+ 搜索框在同一行；**右上角绝对定位的 X 关闭按钮**（`absolute right-5 top-4`）；下方表头（全选 checkbox / 昵称 / 备注 / 分组 / 好友权限）+ 好友行。
- 分组数据来自 `getContactGroups`：预设分组里有成员的 + 好友自定义 group，按名称显示与计数。选中分组过滤右侧列表。

**当前 Qt 状态：**
- 用了 `DialogTitleBar`（有 X，但在顶部标题栏，不是参考的右上角浮动 X）。
- 左分组栏只有一个「全部好友」降级项，无真实分组、无添加分组按钮。
- 右侧表格已有（复选框/昵称/备注/分组/权限 + 全选），但备注/分组/权限是占位。

**差距与修复：**
- [ ] **X 按钮**：确认 `DialogTitleBar` 的 X 为何不显示。根因排查方向：(a) FriendManagerDialog 用了无边框窗口 `Qt::FramelessWindowHint`，X 在 DialogTitleBar 里应可见——检查是否被 `setFixedSize` 裁掉或 z-order 被表格盖住；(b) 若要贴参考，改成右主区右上角浮动 X（`QPushButton` 绝对定位在搜索行右侧），移除/保留标题栏二选一。
- [ ] **真实分组栏**：`setFriendList` 增加接收每个好友的 `group` 字段（MainWindow 传入时从 `m_friendNames` 之外补 group，或新增参数）。按 group 归类，左栏列出「全部好友」+ 各分组（名称 + 计数），点击过滤右侧表格。分组列表数据结构参考 `getContactGroups`：预设分组（有成员的）在前，自定义分组在后。
- [ ] **备注/分组列**：备注列显示好友 remark（Qt 端 `m_friendNames` 即备注来源）；分组列显示 group 字段。
- [ ] **添加分组 / 删除分组**：见下方决策项。

**决策项（需用户确认）——添加/删除分组怎么做：**
- 选项 A（贴参考）：「添加分组」做成 disabled「即将上线」占位，不做删除。零风险，纯还原参考。
- 选项 B（超出参考，做成本地可用）：添加分组 = 本地新增一个空分组名并可把好友拖/选进去；删除分组 = 移除该分组（成员回落到「我的好友」）。因后端无分组协议，这些只能是**本地内存态**，重启不保留（除非另存本地文件）。工作量中等，且和参考行为不一致。

### 2. ＋号按钮（ContactsView 左栏搜索行）

**参考（ContactsView.tsx）目标：**
- ＋按钮尺寸 `h-7 w-7`（约 28px 方形），圆角 `rounded-md`，图标是 lucide `Plus`（细线加号，17px），非文字「＋」。
- 默认背景 `--qq-bg`，hover `--qq-bg-tertiary`；打开时高亮 `--qq-bg-tertiary` + 主色图标。
- 点击展开下拉菜单（不是原生 QMenu 箭头样式）：**创建群聊**（Users 图标）、**加好友/群**（UserPlus 图标）、**闪传文件**（FileUp 图标，disabled「即将上线」）。菜单 `w-32` 圆角浮层。

**当前 Qt 状态：**
- `m_plusButton` 用文字「＋」（全角），`setFixedSize(28,28)`，`contactsPlusButton` 样式是主色实心背景 + 白字。菜单是原生 QMenu。

**差距与修复：**
- [ ] 按钮背景改为次级背景（非主色实心），贴参考的淡底 + hover 变化；图标用细加号（可用 `+` 半角 + 合适字号，或后续换 SVG 图标）。
- [ ] 菜单项顺序对齐参考：创建群聊 / 加好友/群 / 闪传文件(禁用)。给 QMenu 加圆角/内边距 QSS，去掉原生外观。
- [ ] 确认 `::menu-indicator { image:none }` 已生效（不显示下拉小箭头）。

### 3. 加好友（AddFriendDialog）

**参考（AddFriendModal.tsx）目标：**
- 小卡片浮层 `max-w-sm`（约 384px），圆角 `rounded-xl`，padding `p-5`。
- 顶部标题「添加好友」+ 右上角 X（无独立标题栏，X 在标题同行右侧）。
- 搜索行：输入框 placeholder「输入 QQ 号 / 昵称」+ 蓝色「搜索」按钮（带 Search 图标，loading 时 Loader2 转圈）。
- 结果卡片：头像 44px + 昵称 + 签名（signature，缺失显示空行）；右侧「加好友」按钮（UserPlus 图标），loading 转圈，成功后变绿色「已发送」+ disabled。
- 未找到显示红色文案「未找到用户」。

**当前 Qt 状态：**
- 已有搜索行 + 结果卡片（AvatarLabel + 昵称 + QQ 号）+ 加好友按钮 loading/success 态 + 错误行。用 `DialogTitleBar`。
- 差距：用了独立标题栏而非参考的「标题+右上角 X」；结果卡副行显示「QQ:xxx」而非签名；整体尺寸/圆角/内边距可能偏大。

**差距与修复：**
- [ ] 尺寸对齐参考（约 384px 宽，圆角更大 `rounded-xl`，紧凑内边距）。
- [ ] 标题栏样式：可保留 DialogTitleBar，但确认 X 显示；或改为参考式「标题 + 右上角浮动 X」。
- [ ] 结果卡副行：优先显示签名（onSearchResult 目前只有 account/userId/userName，无 signature；若后端搜索结果无签名，保留 QQ 号，标注为偏差）。
- [ ] 搜索按钮加图标 + loading 转圈观感（Qt 可用文字「搜索中…」，已实现）。

### 4. 创建群聊（CreateGroupDialog）—— 差距最大，需重做

**参考（CreateGroupModal.tsx）目标（三种页面，非线性三步）：**

默认页 `step='select'`（538x544，双栏）：
- 左 262px：搜索框；「按分类创建」行（点击进 category 页，右侧「更多 >」）；「选择好友创建」标签；可滚动区：「最近聊天」分组（ChevronDown 折叠标题）+ 成员勾选列表，「我的好友」分组 + 成员列表。成员项：圆形勾选框 + 头像 32px + 昵称。
- 右侧：header「创建群聊」+ 右上角 X；已选成员显示为可移除 chip（头像 + 昵称 + X）；footer「确定」（主色，选了成员才可点）/「取消」。
- **直接选好友 → 确定** 即可建群（不经过分类），群名默认取前 3 个成员昵称拼接。

分类页 `step='category'`（600 宽）：
- header（左返回箭头 + 标题「按分类创建」+ 右 X）。
- 三个分区：熟人与家校 / 兴趣娱乐 / 学习交流，每区 4 列网格分类项（带图标 + 颜色）；部分项可展开子分类（ChevronDown 旋转，子项二级网格）。
- 点普通分类项 → 直接进 info 页；点可展开项 → 展开/收起子类。

填信息页 `step='info'`（538x544）：
- header（左返回 + 标题「填写群信息」+ 右 X）。
- 群名称输入（2-32 字）+ 群头像选择（6 个预设色块头像 + 随机按钮，选中带 ring + Check 角标）。
- 群分类区：显示当前分类 + 「重新选择」，下方前 10 个分类快捷标签。
- 服务声明文案 + 圆形勾选「已阅读并同意《服务声明》」。
- footer：上一步 / 立即创建（名称≥2字 + 选了分类 + 已同意才可点）。

**当前 Qt 状态：**
- 简单线性三步：选成员(可勾选列表) → 选分类(单列表) → 填信息(名称+同意 toggle)。
- 差距：没有「选好友直接建群」的默认双栏页；没有最近聊天/我的好友分区；没有已选成员 chip；分类页不是分区网格+子类展开，只是单列表；info 页没有群头像选择、没有分类快捷标签、没有服务声明文案；每页缺返回/X 头部。

**修复方案（保留 `setCandidateMembers/groupName/selectedMembers/createRequested/selectedCategory` 接口不变）：**
- [ ] 重做为参考的分叉流程：默认 select 页（双栏：左成员勾选列表带分区 + 右已选 chip + 确定/取消），可选进 category 页（分区网格 + 子类展开），再到 info 页（群名 + 预设头像选择 + 分类快捷标签 + 服务声明勾选 + 上一步/立即创建）。
- [ ] 每页头部带 X（select 页右上角 X；category/info 页左返回 + 右 X）。
- [ ] 分类数据用参考的 `GROUP_CATEGORY_SECTIONS`（熟人与家校/兴趣娱乐/学习交流 + 子类），可在 cpp 内内联同一份中文分类表。
- [ ] 预设群头像用参考的 6 个色块（Q/G/C/N/T/群 + 颜色），选中态描边 + Check。
- [ ] 「选好友直接建群」路径：确定时若未选分类，群名默认取前 3 成员昵称拼接。
- [ ] 「最近聊天/我的好友」分区：Qt 端最近聊天可用会话列表推导，若不便获取则先只做「我的好友」一个分区并标注偏差。

## 实施顺序（低风险 → 高风险）

1. **＋号 + 加好友样式微调**（步骤 2、3）：改动小、纯 UI/QSS，先做。
2. **好友管理器分组 + X 按钮**（步骤 1）：先修 X 显示（bug），再做本地分组栏；添加/删除分组按决策项 A/B 定。
3. **创建群聊重做**（步骤 4）：改动最大，最后做，一次性重写 header + cpp。

## 验证方式

- 每个对话框改完立即用 CMake `build-tests-qt`（Qt mingw1310）编译，不靠 Read 回显确认。
- 全部完成后在 Qt Creator 运行，手动过路径：
  1. ＋菜单 → 三项顺序与禁用态；加好友搜索→结果卡→已发送态。
  2. 好友管理器 → X 关闭可见可点；左栏分组过滤；（若做）添加/删除分组。
  3. 创建群聊 → 选好友直接建 / 按分类建两条路径；返回/X；头像选择；服务声明勾选后才能创建。
- 明/暗主题各看一遍（所有样式走 ThemeManager 动态色 + DialogStyle::common()）。

## 交付物

- 修改：`src/views/contactsview.cpp`（＋按钮/菜单样式）、`src/dialogs/addfrienddialog.{h,cpp}`、`src/dialogs/friendmanagerdialog.{h,cpp}`、`src/dialogs/creategroupdialog.{h,cpp}`、必要时 `src/widgets/dialogtitlebar.cpp`（X 显示）。
- 构建通过（CMake + Qt Creator）。
- 若做本地分组：标注为内存态、不持久化（除非额外存本地文件）。

## 决策（已确认 2026-07-15）

1. **好友管理器「添加/删除分组」= 选项 B 增强版**：新增分组 / 删除分组按钮真正可用，且**后端持久化**（存进现有好友存储：SQLite `saveFriendsToSqlite` 或旁挂一份分组映射，重启保留）。新增=输入分组名建空分组；删除=移除分组、成员回落「我的好友」；好友可改所属分组。需要给 `ClientStorage` / 好友存储增加 `好友ID→分组名` 的读写。
2. **创建群聊成员列表 = 做「最近聊天 / 我的好友」两区**：最近聊天从 `m_sessionModel`（会话列表）推导最近私聊过的人，其余归「我的好友」。
3. **X 按钮 = 保留 DialogTitleBar，只修 X 不显示的 bug**，不改成浮动 X。

### 分组持久化落地细节（步骤 1）

- 数据：`QMap<QString/*friendId*/, QString/*groupName*/>` + `QStringList/*自定义分组名，含空分组*/`。
- 存储：优先扩展 `ClientStorage`（新增 `saveFriendGroups/loadFriendGroups`，SQLite 一张 `friend_groups(friendId, groupName)` 表 + 一张 `custom_groups(name)` 表存空分组）；MainWindow 持有 `m_friendGroups`/`m_customGroups`，在 `saveFriends()` 附近一起存。
- FriendManagerDialog：`setFriendList` 增传分组信息；新增信号 `createGroupRequested(name)` / `deleteGroupRequested(name)` / `moveFriendToGroup(friendId, group)`，由 MainWindow 落地并回存。
