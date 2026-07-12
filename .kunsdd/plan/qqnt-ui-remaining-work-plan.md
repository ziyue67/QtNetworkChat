# QQNT UI 剩余工作计划（未完成项收口）

## 背景

基于对分支 `qqnt-ui-frontend` 代码的核对，结合已有的两份计划
（`qqnt-ui-recovery-plan.md` 细化版、`the-user-referenced-...-workspace-file-p.md` 合并版），
本文件只列出**尚未完成**的部分，并给出实现顺序与验收点。已完成的能力不再重复。

核对结论：8 个阶段全部处于 PARTIAL 状态，没有任何一个阶段完全收口。
两处最关键的欠账是**阶段 1（会话模型从未真正分离）**和**阶段 2（MessagesView 状态接口不存在、旧控件仍在并行驱动）**，
它们决定“新消息页是否为唯一聊天表面”这一核心目标能否成立。

## 进度追踪

- [x] 任务 A：会话模型分离 —— 已完成（`m_sessionModel` 独立，`refreshSessionList()`，`__public__` 路由，SessionIdRole 优先）
- [x] 任务 B：MessagesView 状态接口 —— 已完成（`setSessionState/setComposerState/scrollChatToBottom/setCurrentSessionId`，滚动统一走 `scrollActiveChatToBottom()`）
- [x] 任务 C：气泡数据角色补全 —— 已完成（`decorateChatItem` 写 timestamp/readStatus；10 分类表情面板）
- [x] 任务 D：FavoritesView 接入 + 头像菜单补全 —— 已完成（`refreshFavoritesView()`/`onFavoriteSelected()`；头像禁言走 MuteDurationDialog + `sendServerGroupMemberMute`，管理员走 `requestServerGroupMemberUpdate`）
- [x] 任务 E：ProfileView 动作 + 对话框统一 —— 已完成（editProfileRequested→内联改昵称，changeAvatarRequested→onUploadAvatar；登录/注册流改用 `LoginWindow`/`RegisterWindow`（frameless + `DialogTitleBar` + 主题 token），删除 `main.cpp` 内联硬编码 `LoginDialog`，`LoginCredentialStore` 并入 `LoginWindow` 持久化账号；`main.cpp` 启动即 `applyQqntThemeStyleSheet` 使 modeDialog/QMessageBox 共享主题；其余弹窗此前已用 DialogTitleBar）
- [x] 任务 F：截图多显示器与 DPR —— 已完成（逐屏 `grabWindow(0)` 合成虚拟桌面画布，按最大 DPR 缩放，虚拟桌面左上角为原点处理负坐标）
- [x] 任务 G：主题收口与静态检查 —— 已完成（`theme_qss_consistency_test` 校验亮暗 token 与 `ThemeManager` TOKENS 表对齐，并静态检查关键 `dialogTitleBar*` 选择器↔`setObjectName` 覆盖；注册为 ctest `ThemeQssConsistency`）
- [x] 任务 H：观测与新测试 —— 已完成（观测既有；新增 `session_list_builder_test`/`screenshot_geometry_test`/`theme_qss_consistency_test`，分别注册为 ctest `SessionListOrdering`/`ScreenshotGeometryMapping`/`ThemeQssConsistency`；Session 排序逻辑抽到纯头 `include/sessionlistbuilder.h`，`refreshSessionList()` 复用之）

每个任务完成后均通过 CMake Debug 构建验证。

## 未完成清单（按依赖顺序）

### 任务 A：会话模型分离（阶段 1 收口）—— 最高优先级

现状：`MessagesView` 自带 `m_sessionModel`（`messagesview.cpp:100`），
但 `MainWindow::setupQQNT` 用 `m_messagesView->setSessionModel(m_userListModel)`
（`mainwindow.cpp:8953`）覆盖，会话列表仍由旧 `m_userListModel` 驱动。

要做：
- 在 `MainWindow` 新增独立成员 `m_sessionModel`（`mainwindow.h:359-361` 附近），
  只服务会话列表，不再复用 `m_userListModel`。
- 删除 `setSessionModel(m_userListModel)` 的覆盖，改为绑定 `m_sessionModel`。
- 将 `refreshFriendList()` 明确拆为 `refreshSessionList()` 与 `refreshContactsView()`
  两个函数（目前功能上是 `refreshFriendList()` + `refreshContactsAndProfile()`，需正名并解耦）。
- `refreshSessionList()` 只生成公共群 / 私聊 / 本地群真实会话，
  按置顶、最近时间、名称排序；不插入“我的好友/群聊/在线成员”标题行。
- 补齐 `appendSessionItem`（`mainwindow.cpp:8245-8260`）缺失的 `avatarPath` 角色。
- 会话选择信号改用稳定的 `SessionIdRole`，替换现在的 `Qt::UserRole + 1`
  （`onPrivateChat` `mainwindow.cpp:3645`）。

验收：左栏无紫色 `?`；真实昵称首字母、未读、在线、置顶、时间、摘要正确；
联系人搜索只过滤联系人页；会话列表与联系人页各自独立刷新。

### 任务 B：MessagesView 状态接口 + 单一聊天表面（阶段 2 收口）

现状：计划要求的 `setSessionState / setComposerState / scrollChatToBottom /
setCurrentSessionId` 在 `messagesview.h` 中不存在；滚动仍打到旧
`ui->chatListView->scrollToBottom()`（`mainwindow.cpp:3473`、`7497`）；
`refreshComposerState`（`2873`）同时手动更新旧 `ui->*` 和新 composer。

要做：
- 在 `MessagesView` 增加最小状态接口：
  - `setSessionState(sessionId, title, subtitle, hint, loading, empty)`
  - `setComposerState(enabled, placeholder, stateText, mentionCandidates)`
  - `scrollChatToBottom()`
  - `setCurrentSessionId(sessionId)`
- 所有滚动到底部改调 `m_messagesView->scrollChatToBottom()`，移除对
  `ui->chatListView` 的直接滚动。
- 新增单一兼容函数集中访问旧隐藏控件；业务方法不得直接设置旧控件可见性/文本。
- 消息追加、历史加载、过滤、清空、文件状态、连接状态、E2E 状态统一走新接口。

验收：三类会话切换后标题、历史、草稿占位、成员候选、发送状态同步；
隐藏旧输入框和旧列表不改变可见行为。

### 任务 C：气泡数据角色补全（阶段 3 收口）

现状：`ChatBubbleDelegate` 角色齐全（`chatbubbledelegate.h:13-30`），
但 `decorateChatItem`（`mainwindow.cpp:8044-8061`）只写了
senderId/name/avatar/outgoing/system，未写 timestamp/readStatus/quoted/forwarded/grouped。

要做：
- 在 `decorateChatItem` 补齐 timestamp、readStatus、quoted、forwarded、grouped 角色写入。
- Composer 表情：把 `onInsertEmoji`（`mainwindow.cpp:4701`）的平铺 `QMenu`
  替换为 10 分类表情面板（最近/超级/小黄脸/手势/爱心/动物/自然/食物/物品/符号）。

验收：气泡显示时间、已读状态、引用、转发、连续消息收紧；表情面板分类可用。

### 任务 D：FavoritesView 接入 + 头像菜单补全（阶段 4 收口）

现状：`FavoritesView` 已加入栈（`mainwindow.cpp:8957/8963`），
但从未调用 `get_local_favorite_messages`，`favoriteSelected` 未连接；
`onAvatarActionRequested`（`4849`）的禁言/管理员为桩（`4871`）；
`readLocalChatActions`（`4615`）未喂入可见性数组。

要做：
- 在 `setupQQNT` 连接 `m_favoritesView->favoriteSelected`；
  用 `get_local_favorite_messages` 填充真实收藏项；点击后切换会话并定位原消息。
- 头像禁言/管理员改为经 `qqnt_engine_command_router` 的成员权限命令，
  按会话类型和权限设置可见/可用；不再留桩。
- 让本地操作可见性能真实驱动 `setLocalActionState`（需后端补 actions 数组或前端按类型推导）。

验收：收藏页显示真实收藏并可跳转；头像菜单各项按权限正确启用/禁用并生效。

### 任务 E：ProfileView 动作 + 对话框统一（阶段 5 收口）

现状：`editProfileRequested`、`changeAvatarRequested`（`profileview.cpp:89/94`）
未在 `setupQQNT` 连接（仅 `logoutRequested` 已连 `9022`）；签名未绑真实数据；
登录/注册对话框（`main.cpp:151-595`）用内联硬编码样式，未用 `DialogTitleBar` 和主题 token。

要做：
- 连接并实现 `editProfileRequested`、`changeAvatarRequested`，复用既有后端/存储流程。
- ProfileView 绑定真实签名字段。
- 登录、注册、图片预览、转发、通知筛选、好友/群管理窗口统一 `DialogTitleBar`
  与亮暗 QSS，去掉内联硬编码样式。

验收：资料编辑/换头像可用并持久化；各弹窗标题栏一致、亮暗切换不丢样式。

### 任务 F：截图多显示器与 DPR（阶段 6 收口）

现状：`onCaptureScreenshot`（`mainwindow.cpp:5181`）已算出 `virtualRect`
（`5194-5198`），但只 `primary->grabWindow(0)`（`5216`），无逐屏拼接、无 DPR、无负坐标映射。
恢复守卫已完整（各退出路径都调 `restore_main_window`）。

要做：
- 用所有 `QGuiApplication::screens()` 的 geometry 与 devicePixelRatio 构造虚拟桌面图：
  每屏各自 `grabWindow(0)`，以虚拟桌面左上角为原点拼接，处理负坐标。
- `ScreenshotCaptureWindow` 接收源图尺寸、虚拟几何与缩放映射，
  框选结果对应正确物理像素。

验收：单屏、双屏、负坐标副屏；取消/确认/无屏幕/抓屏失败四条路径均恢复窗口。

### 任务 G：主题收口与静态检查（阶段 7 收口）

现状：全局 QSS 已只从 `applicationDirPath()/ui/` 读取并记日志（DONE）；
但无 `theme.css` 与两份 `.qss` 的 token 对齐校验；无 QSS 选择器↔objectName 的静态/单元检查。

要做：
- 核对 `style-qqnt.qss` 与 `style-qqnt-dark.qss` 的亮暗 token 与 `ThemeManager` 一致。
- 增加静态检查或单元测试：关键 QSS 选择器必须存在对应 `objectName`；关键控件 object name 非空。

验收：Qt Creator 与直接运行 exe 在亮暗主题下视觉一致；检查项通过。

### 任务 H：观测与新测试（阶段 0 + 阶段 8 收口）

现状：自动登录等待已就绪（DONE）；但日志缺当前路由/会话 ID/模型行数/截图恢复路径；
无 `m_qqntRoot`、`m_messagesView`、`m_viewStack`、`m_appNav` 非空断言；
缺 Session 排序/角色、会话切换、Composer 状态、截图坐标映射的新单元测试
（现有 `chat_session_manager_test`、`composer_manager_test`、`qqnt_backend_service_test`、E2E 测试保留）。

要做：
- 统一运行日志补记：QSS 路径、当前路由、当前会话 ID、会话/聊天模型行数、截图恢复路径。
- 主窗口启动后增加非空断言。
- 新增 Qt 单元测试：Session 排序/角色、会话切换、Composer 状态、截图坐标映射；
  注册进 CMake ctest。

验收：日志可诊断；断言生效；新测试与既有测试全部通过。

## 执行顺序与依赖

1. 任务 A（模型分离）→ 2. 任务 B（状态接口/单一表面）：这两项是地基，先做。
2. 任务 C（气泡角色）、任务 D（收藏/头像菜单）依赖 A/B 完成后接入。
3. 任务 E（资料/对话框）、任务 F（截图）相对独立，可并行。
4. 任务 G（主题检查）、任务 H（观测/测试）最后收口，贯穿验证。

## 边界与假设（延续原计划）

- 视觉与交互以 `D:\C++VS pro\tauri-qqnt` 为准，不保留旧三栏布局。
- 后端命令、数据协议、现有账号与 SQLite 结构不做破坏性迁移。
- 每阶段执行 qmake Debug + CMake Debug 构建，用当前真实账号手工回归。
- 截图、`.kunsdd/`、本地计划、SQLite 数据、构建目录不提交 Git；
  每个任务独立提交并推送。
