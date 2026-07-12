# QtNetworkChat 联系人/群组 UI 对照 Tauri-QQNT 补齐计划

## 摘要

以 `D:\C++VS pro\tauri-qqnt` 的 React/TSX 前端为唯一视觉与交互对照，把 QQNT 风格的**联系人视图、添加联系人、好友管理、全局搜索、创建群聊**和**群组详情/成员/管理** UI 完整迁移到当前 Qt C++项目（\`D:\\C++VS pro\\QtNetworkChat\`）。保持现有后端/协议逻辑不变，仅增强 UI 层。

## 当前缺口（与 Tauri 对照逐项核对）


| Tauri 前端组件                                                              | 当前 Qt 状态                              | 缺口说明                                                   |
| ----------------------------------------------------------------------- | ------------------------------------- | ------------------------------------------------------ |
| `ContactsView.tsx`（联系人主视图）                                              | 已有 `ContactsView`，但仅标题+搜索+Tab 列表      | 缺左侧“好友通知/群通知/好友管理器/Plus 菜单/好友-群聊切换”结构，缺右侧“联系人资料卡/群资料卡” |
| `ContactList.tsx`                                                       | 直接用的 `QListView`+`QStandardItemModel` | 缺头像、在线状态、签名、分组、hover/selected 色                        |
| `ContactCard.tsx`                                                       | 无                                     | 缺好友/群聊统一资料卡、成员列表、群公告、发送消息按钮                            |
| `AddFriendModal.tsx`                                                    | 已有 `AddFriendDialog`                  | 缺头像、状态/签名展示、按 Enter 搜索、搜索中 loading、已发送成功态              |
| `FriendManagerModal.tsx`                                                | 已有 `FriendManagerDialog`              | 缺左侧分组过滤、表头、批量选择、备注/分组列、权限状态                            |
| `GlobalSearchModal.tsx`                                                 | 已有 `GlobalSearchDialog`               | 缺 Tab 分类、用户/群聊/机器人分类、群详情侧栏、加入按钮                        |
| `CreateGroupModal.tsx`                                                  | 已有 `CreateGroupDialog`                | 缺三步骤交互（选择成员 → 分类 → 群信息）、分类选择、头像/协议勾选                   |
| `ContactNoticePanel.tsx`                                                | 无                                     | 缺好友通知/群通知空态面板                                          |
| `GroupMemberSidebar.tsx`                                                | 无                                     | 缺群成员侧栏、搜索、公告、角色排序、右键菜单（禁言/设管理员/@/私聊/加好友/修改群昵称/举报/屏蔽）   |
| `GroupNicknameDialog.tsx`                                               | 已有 `GroupNicknameDialog`              | 功能基本对得上，需风格统一                                          |
| `MemberProfileCard.tsx` / `MuteDurationDialog.tsx` / `EssencePanel.tsx` | 已有同名 Qt 对话框                           | 需按 QQNT 风格重新排版、补齐成员资料/自定义禁言/精华消息                       |


另外，Qt `ui/mainwindow.ui` 仍是旧版三栏布局，需要与新版侧栏、聊天区、联系人视图保持一致（本计划只聚焦于联系人/群组 UI 改造，不扩大重写整个主窗口）。

## 实现步骤

### 1. 复用/确认设计 token

- 以 `include/theme/thememanager.h` 中已有 token 为准：背景、边框、主色、hover、success、danger、字号等。
- 新增需要但缺少的 token（如 avatar/状态点/selected/quiet-hover）并同步更新 `ThemeManager`。
- 不对现有暗色/亮色切换机制做大规模改动。

### 2. 复用现有基础设施

- 已存在 `src/widgets/dialogtitlebar.cpp`、`src/widgets/avatarlabel.cpp`，后续新建对话框统一使用无边框 + `DialogTitleBar`。
- 已存在 `src/dialogs/*` 系列，优先以“重构界面+补齐交互”方式改造，非必要不新建。
- 已存在 `FriendManager` / `GroupManager` / `Client` / `Server` 业务逻辑，UI 只调用已有 signal/slot，不重复实现协议。

### 3. 联系人视图改造（`src/views/contactsview.cpp`）

按 `ContactsView.tsx` 重构为：左侧会话式列表区 + 右侧详情区。

左侧（固定 260px，参考 `--qq-session-width`）：

- 顶部搜索条（只读输入框 + Plus 下拉按钮）。
- Plus 下拉菜单：创建群聊、加好友/群、闪传文件（占位禁用）。
- “好友管理器”按钮。
- “好友通知 / 群通知”两行入口，带未读红点。
- 底部“好友 / 群聊”切换 Tab。
- 中间列表区域使用新组件 `ContactListWidget`（见下）。

右侧详情区：

- 未选择时显示占位文案。
- 选择好友：使用 `ContactCard`。
- 选择群聊：使用 `ContactCard` 群聊模式。
- 点击通知入口：使用 `ContactNoticePanel` 空态。

信号：

- `friendSelected(userId)`、`groupSelected(groupId)`、`openAddFriend()`、`openCreateGroup()`、`openFriendManager()`、`openGlobalSearch()`。

### 4. 联系人列表项（新增 `src/widgets/contactlistwidget.cpp`）

按 `ContactList.tsx` 实现：

- 委托 `ContactListItemDelegate` 绘制：圆形头像（昵称首字母 fallback）、昵称、在线状态文字、签名。
- 好友/群聊两种模式（群聊显示人数/签名，不显示在线状态）。
- 支持分组标题项（如“全部好友”“我的好友”）。
- 选中态 `session-selected`，hover 态 `session-hover`。

### 5. 联系人资料卡（新增 `src/widgets/contactcard.cpp`）

按 `ContactCard.tsx` 实现：

- 大头像、昵称、下方状态/签名。
- 好友显示在线状态、备注；群聊显示群公告卡片、成员缩略图网格（最多 12 人）。
- 底部主按钮：好友为“发送消息”，群聊为“发送群消息”。
- 点击按钮跳转到 Message 视图并打开对应会话。

### 6. 添加好友对话框改造（`src/dialogs/addfrienddialog.cpp`）

按 `AddFriendModal.tsx` 改造：

- 搜索框 + 蓝色搜索按钮；支持回车搜索。
- 搜索结果展示头像、昵称、签名，右侧“加好友”按钮。
- 搜索中显示 loading；失败显示红色文案；已发送显示成功态。
- 保持 `searchRequested(QString)`、`addFriendRequested(QString)` 信号不变。

### 7. 好友管理器对话框改造（`src/dialogs/friendmanagerdialog.cpp`）

按 `FriendManagerModal.tsx` 改造：

- 左侧 200px 分组列表（全部好友 + 预设/自定义分组）。
- 右侧表头：全选/昵称/备注/分组/好友权限。
- 行委托：头像、昵称、备注、分组、在线状态列。
- 全选/单选逻辑；删除选中时弹出确认提示。
- 添加好友按钮复用 #6 的 `AddFriendDialog`。

### 8. 全局搜索对话框改造（`src/dialogs/globalsearchdialog.cpp`）

按 `GlobalSearchModal.tsx` 改造：

- 顶部搜索 + 标签页：全部 / 用户 / 群聊 / 小程序 / 机器人。
- “小程序/机器人”阶段先占位为即将上线。
- 群聊结果项支持点击打开右侧详情面板（310px 滑入/固定），面板内显示群头像、人数、分类标签、介绍、群号、加入按钮。
- 复用 `ContactCard` / `ContactListWidget` 绘制头像。

### 9. 创建群聊对话框改造（`src/dialogs/creategroupdialog.cpp`）

按 `CreateGroupModal.tsx` 改造为三步弹窗：

1. **选择成员**：左侧搜索 + 好友列表，右侧已选成员头像；支持取消选择。
2. **选择分类**：熟人/家校、兴趣娱乐、学习交流等（按 Tauri 的 `GROUP_CATEGORY_SECTIONS`）。
3. **填写信息**：群名称、群头像、同意协议勾选、创建按钮。

保持输出信号：

- `createRequested(groupName, selectedMemberIds)`。

### 10. 通知面板（新增 `src/widgets/contactnoticepanel.cpp`）

按 `ContactNoticePanel.tsx` 实现：

- 顶部标题 + 筛选/清空按钮。
- 中间空态：圆形 border + 铃铛图标 + “暂无通知”。
- 后续可扩展为真实通知列表，本次先补齐空态 UI 与交互入口。

### 11. 群成员侧栏（新增 `src/widgets/groupmembersidebar.cpp` + 对话框）

这是“群组没搞好”的核心：

- 在聊天视图右侧增加 256px 群成员侧栏（按 `GroupMemberSidebar.tsx`）。
- 显示：群成员标题+人数、群公告摘要、成员搜索、成员列表。
- 成员项委托：头像、群昵称/昵称、角色徽章（owner/admin/member）、禁言中红色提示。
- 右键菜单：发送消息、@TA、查看资料、添加好友、修改群昵称、设置禁言（10分钟/1小时/12小时/1天/自定义）、解除禁言、设置/取消管理员、举报、屏蔽。
- 权限判定复用已有 `GroupManager::memberContextMenuPlan`。

需要新增/改造对话框：

- `GroupNicknameDialog`：风格统一即可。
- `MuteDurationDialog`：提供自定义时长（分钟/小时/天）输入。
- `MemberProfileCard`：群成员资料卡。
- `EssencePanel`：精华消息展示。

### 12. 主窗口集成

在 `MainWindow` 中：

- ContactsView 的 `friendSelected` / `groupSelected` 映射到 `openPrivateSession` / 打开群会话。
- ContactsView 的 `openAddFriend` 打开 `AddFriendDialog`。
- ContactsView 的 `openCreateGroup` 打开 `CreateGroupDialog`。
- ContactsView 的 `openFriendManager` 打开 `FriendManagerDialog`。
- 顶部 Plus 搜索框 focus/click 打开 `GlobalSearchDialog`。
- 群成员侧栏的“发送消息”走 `openPrivateSession`；“@成员”调用 `ComposerWidget::insertMention`；“修改群昵称/禁言/管理员”通过 `Client` 发送已有命令。

### 13. 样式表与 QSS

- 新建/复用 `ui/style-qqnt-contact.qss`（或整合到现有 `style-qqnt.qss`）。
- 为联系人视图、对话框、右键菜单、侧栏提供一致的 QQNT 样式。
- 优先使用 `ThemeManager` 动态刷新，避免硬编码颜色。

### 14. 构建与测试

- 将新增/修改的 `.cpp/.h` 加入 `CMakeLists.txt` 与 `QtNetworkChat.pro`。
- 提供至少一个截图脚本，验证联系人视图、添加好友、创建群聊、群成员侧栏在明/暗色主题下效果。
- 手动测试路径：
  1. 打开联系人视图 → 好友/群聊切换 → 选择项显示资料卡。
  2. 点击 Plus → 创建群聊 → 三步流程 → 创建后跳到消息视图。
  3. 点击 Plus → 加好友 → 搜索 → 发送请求。
  4. 点击好友管理器 → 分组过滤 → 删除好友（二次确认）。
  5. 点击搜索框 → 全局搜索 → 切换 Tab → 打开群详情。
  6. 进入群聊 → 展开群成员侧栏 → 右键成员 → 禁言/修改群昵称/设管理员。

## 优先顺序

1. **联系人视图骨架 + ContactListWidget + ContactCard**（先让主页看起来像 Tauri 对照）。
2. **AddFriendDialog / CreateGroupDialog / FriendManagerDialog / GlobalSearchDialog 改造**（补齐四大对话框）。
3. **ContactNoticePanel 占位**（通知入口不再空点）。
4. **GroupMemberSidebar + 群管理菜单 + 相关对话框**（解决“群组没搞好”）。
5. **样式统一、构建脚本、截图验证**。

## 风险与限定

- 不修改 `Client`/`Server`/`FriendManager`/`GroupManager` 的核心业务，仅确保 UI 调用已有信号/槽。
- 若后端缺少某个能力（如“修改群昵称”的协议字段），发现后记录到 TODO 并通知用户，而不是凭空造协议。
- 显示的“在线/离线”状态依赖 `ChatUser` / `knownUsers` 数据结构，保持与现有模型一致。
- 表情、文件闪传等 Tauri 中标记为“即将上线”的功能，Qt 版继续做占位处理，不本次实现。

## 交付物

- 新增/修改的头文件与实现文件清单：
  - `include/views/contactsview.h`, `src/views/contactsview.cpp`
  - `include/widgets/contactlistwidget.h`, `src/widgets/contactlistwidget.cpp`
  - `include/widgets/contactcard.h`, `src/widgets/contactcard.cpp`
  - `include/widgets/contactnoticepanel.h`, `src/widgets/contactnoticepanel.cpp`
  - `include/widgets/groupmembersidebar.h`, `src/widgets/groupmembersidebar.cpp`
  - `include/dialogs/addfrienddialog.h`, `src/dialogs/addfrienddialog.cpp`
  - `include/dialogs/creategroupdialog.h`, `src/dialogs/creategroupdialog.cpp`
  - `include/dialogs/friendmanagerdialog.h`, `src/dialogs/friendmanagerdialog.cpp`
  - `include/dialogs/globalsearchdialog.h`, `src/dialogs/globalsearchdialog.cpp`
  - `include/dialogs/groupnicknamedialog.h`, `src/dialogs/groupnicknamedialog.cpp`
  - `include/dialogs/mutedurationdialog.h`, `src/dialogs/mutedurationdialog.cpp`
  - `include/dialogs/memberprofilecard.h`, `src/dialogs/memberprofilecard.cpp`
  - `include/dialogs/essencepanel.h`, `src/dialogs/essencepanel.cpp`
  - `ui/style-qqnt-contact.qss`（或合并到现有样式表）
- 构建通过（CMake + qmake）。
- 至少 6 个场景截图保存到 `screenshots/`。

