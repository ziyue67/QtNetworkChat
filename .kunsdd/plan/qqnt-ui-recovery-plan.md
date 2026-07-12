# QQNT UI 恢复计划（细化版）

## 目标与边界

将当前 Qt Widgets 客户端恢复为与 `D:\C++VS pro\tauri-qqnt` 核心页面一致的 QQNT UI，同时保留现有 `Client`、SQLite、Redis、E2E、文件传输与 `QQNTBackendService` 协议。

新 UI 是唯一可见入口；旧 `ui/mainwindow.ui` 仅在迁移期提供隐藏兼容控件，不再新增可见功能。验收使用当前真实账号；截图、`.kunsdd/`、构建产物不提交 Git。

## 阶段 0：可重复启动与观测

- 修复 `QTNETWORKCHAT_DEBUG_AUTO_LOGIN`：启动本地服务后使用事件循环等待 `Client::login` 成功、失败或超时，成功才构造 `MainWindow`。
- 统一运行日志：记录 QSS 实际路径、当前路由、当前会话 ID、会话模型行数、聊天模型行数、截图恢复路径。
- 主窗口启动后断言：`m_qqntRoot`、`m_messagesView`、`m_viewStack`、`m_appNav` 非空；旧 `centralwidget` 已隐藏。
- 验收：qmake/CMake Debug 构建通过；固定自动登录可稳定进入消息页且无 SIGSEGV。

## 阶段 1：会话、联系人模型分离

- 新增独立 `m_sessionModel`，只供 `MessagesView::sessionListView()` 使用；保留 `m_groupMemberModel`；废弃用 `m_userListModel` 同时承担会话和联系人。
- 定义稳定 Session 数据角色：`id`、`name`、`avatarPath`、`lastMessage`、`lastTime`、`unread`、`pinned`、`mention`、`online`、`kind`。
- `refreshFriendList()` 拆为：
  - `refreshSessionList()`：生成公共群、私聊、本地群的真实会话；按置顶、最近时间、名称排序；不插入“我的好友/群聊/在线成员”标题。
  - `refreshContactsView()`：分别填充 `ContactsView` 的好友和群聊模型，联系人搜索仅过滤该视图。
- 删除 `SessionItemDelegate` 对空角色的隐式 fallback；它仅绘制真实会话。无会话时由 `MessagesView` 显示“暂无会话”。
- 会话选择信号改为传递 Session ID；`onPrivateChat` 支持公共群、私聊、本地群三种 ID，避免依赖 `Qt::UserRole + 1`。
- 验收：左栏不再有紫色 `?`；真实昵称首字母、未读、在线、置顶、时间和摘要正确显示；联系人页仍能搜索并进入会话。

## 阶段 2：新消息页成为唯一聊天表面

- 为 `MessagesView` 增加最小状态接口：
  - `setSessionState(sessionId, title, subtitle, hint, loading, empty)`
  - `setComposerState(enabled, placeholder, stateText, mentionCandidates)`
  - `scrollChatToBottom()`
  - `setCurrentSessionId(sessionId)`
- `MainWindow` 的消息追加、历史加载、过滤、清空、文件状态、连接状态、E2E 状态均更新新聊天列表和新标题标签。
- 迁移旧 `ui->messageEdit` 的 Enter、Shift/Ctrl+Enter、Esc、粘贴、右键菜单、草稿、焦点和禁言逻辑到 `ComposerWidget`。
- 迁移旧 `ui->chatListView` 的双击媒体、右键菜单、选中、多选、加载历史和滚动到底部逻辑到 `MessagesView::chatListView()`。
- 旧隐藏控件只经统一兼容函数访问；禁止业务方法直接决定隐藏控件的可见状态或文本。
- 验收：公共群、私聊、本地群切换后标题、历史、草稿占位、成员候选、发送状态都同步；隐藏旧输入框和旧列表不会改变可见界面行为。

## 阶段 3：聊天视觉与 Composer 对齐

- 对齐 `SessionList.tsx`、`SessionItem.tsx`、`ChatPanel.tsx`：左导航 `64px`、会话列 `260px`、标题栏 `40px`，以及会话项背景、间距与选中态。
- 统一 `ChatBubbleDelegate` 数据角色与 `decorateChatItem()`：outgoing、system、timestamp、read status、quoted text、forwarded、grouped、media kind、preview、open path。
- 完善气泡：自己消息主色、对方消息浅灰、系统消息居中、连续消息收紧头像/间距；图片缩略图、文件卡片、引用块、转发标签均由 delegate 绘制。
- `ComposerWidget` 使用稳定的 `28x28` 图标按钮；补齐表情分类面板和真实成员 `@` 补全；修正 QSS 与 `objectName` 不匹配问题。
- 验收：主消息页无 Fusion 默认控件混入；气泡、文件、图片、输入栏与 tauri-qqnt 对比截图一致。

## 阶段 4：后端动作、文件和收藏

- 消息右键统一经 `MessagesView` 信号进入 `MainWindow`，再调用 `QQNTBackendService`：复制、转发、收藏/取消、多选、引用、精华/取消、撤回、删除；成功后刷新模型或重新读取本地操作记录。
- 头像右键执行私聊、@、资料、加好友、群昵称、禁言/解除、管理员、屏蔽、举报，并按会话类型和权限设置可见/可用状态。
- 多选底栏从新聊天列表读取选中项，执行转发、删除、收藏并退出多选模式。
- 图片支持预览、Base64 复制、另存为、打开文件夹、转发；文件支持打开、打开所在文件夹、传输状态更新和拖放发送。
- `FavoritesView` 调用 `get_local_favorite_messages` 填充真实收藏项，点击后切换会话并定位原消息。
- 验收：每个菜单动作验证成功和不可执行/失败提示路径；操作后 UI 立即刷新。

## 阶段 5：联系人、资料、设置及窗口

- `ContactsView` 绑定好友、群聊模型与搜索；好友单击进入私聊，群聊单击进入群会话，并提供明确空状态。
- `ProfileView` 绑定真实头像、昵称、账号、签名、好友数、群数、消息数和二维码；编辑资料、换头像、退出登录沿用既有后端/存储流程。
- `SettingsView` 绑定主题、通知、快捷键、账号、关于配置的读取和持久化；主题切换发射 `ThemeManager::themeChanged`，全部新视图即时刷新。
- 登录、注册、图片预览、转发、通知筛选、好友/群管理窗口统一自定义标题栏和亮暗 QSS。
- 验收：五个核心路由均显示真实数据或明确空状态；退出登录返回登录入口；亮暗切换不丢失当前页面。

## 阶段 6：截图与多显示器窗口壳层

- 抽取截图流程为单一恢复守卫：隐藏主窗口后，任何取消、错误、完成、异常退出路径都调用 `restore_main_window` 并恢复窗口。
- 使用所有 `QGuiApplication::screens()` 的 geometry 和 DPR 构造虚拟桌面图：每屏分别 `grabWindow(0)`，以虚拟桌面左上角为原点拼接，并处理负坐标。
- `ScreenshotCaptureWindow` 接收源图尺寸、虚拟几何与缩放映射，框选结果对应正确物理像素。
- 覆盖层为无边框、置顶工具窗口；主窗口隐藏后再抓屏，覆盖层只在抓屏之后出现。
- 保存裁剪图至临时目录，使用既有文件投递路径插入当前 Composer；取消不产生文件。
- 验收：单屏、双屏、负坐标副屏；取消、确认、无屏幕、抓屏失败四条路径均恢复窗口。

## 阶段 7：主题收口与自动检查

- 全局 QSS 只从 `QCoreApplication::applicationDirPath()/ui/` 读取，读取失败写日志并显示可诊断状态。
- 组件局部样式只覆盖自身运行时 token，禁止 `QWidget { ... }` 这类影响后代的宽泛规则。
- 对齐 `theme.css` 亮暗 token：背景、边框、文本、主色、危险色、成功色、圆角、滚动条、hover、disabled。
- 增加静态检查或单元测试：关键 QSS 选择器必须存在对应 `objectName`；关键控件必须有非空 object name。
- 验收：Qt Creator 和直接运行 exe 在亮暗主题下视觉一致，日志确认加载同一主题文件。

## 阶段 8：测试与提交

- 新增 Session 排序/角色、会话切换、Composer 状态、截图坐标映射的 Qt 单元测试；继续运行 `chat_session_manager_test`、`composer_manager_test`、`qqnt_backend_service_test` 和端到端聊天测试。
- 每阶段执行 qmake Debug、CMake Debug、当前真实账号手工回归，并保存本地截图：主消息、联系人、收藏、设置、资料、右键菜单、文件预览、截图覆盖层、亮暗主题。
- 每个阶段独立提交并推送到 `rit-v2`；只包含源代码、构建配置和必要文档，不包含截图、本地计划、SQLite 数据或构建目录。

## 已锁定的假设

- 视觉与交互以 `D:\C++VS pro\tauri-qqnt` 为准，当前 Qt 版本不保留旧三栏布局。
- 后端命令、数据协议、现有账号数据和 SQLite 结构不做破坏性迁移。
- 使用当前真实账号验收，因此测试数据变化可影响截图内容，但不应影响布局、状态和功能断言。
