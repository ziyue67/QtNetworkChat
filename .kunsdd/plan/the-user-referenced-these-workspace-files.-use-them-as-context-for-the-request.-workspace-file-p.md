# QQNT UI 修复计划（rit-v2）—— 合并版

## 摘要

当前 Qt C++ 项目与 `tauri-qqnt` 前端共用同一套 C++ 后端引擎：36 条 QQNT 命令已在 `qqnt_engine_command_router.cpp` 路由，登录/聊天/群/文件/E2E/设置等后端能力均已就绪。`QQNTBackendService` 还提供了本地消息操作、图片 Base64、文件保存、截图等命令。分支 `rit-v2` 正在重写 UI，但主界面和聊天面板仍与 `tauri-qqnt` 前端不一致，且后端已具备的能力尚未全部接入到 UI。本计划把两个已有计划合并，形成一份**从视觉到后端能力**的完整路线图，按阶段实现，逐步验收。

## 一、已确认的后端能力（可直接复用）

- **网络协议**：`src/qqnt_engine_command_router.cpp` 已路由 36 条命令：
  登录/注册/注销、连接/断开、在线用户、好友搜索/申请/同意、私聊/群聊、建群、群公告、成员权限、群精华、消息收藏、撤回、禁言、成员资料、文件/图片发送/取消/断点续传、E2E 状态、身份发布/信任、会话轮换、资料更新、设置同步。
- **本地消息操作**：`src/qqnt_backend_service.cpp` 已实现：
  本地删除消息、收藏状态、表情/置顶备注、多选、引用、精华、撤回、转发、查看成员资料、举报、拉黑、设置群昵称、本地操作记录读取。
- **文件/图片工具**：读取图片 Base64、保存文件到目录、保存 Base64 文件到目录。
- **截图**：`QQNTBackendService` 已有本地截图命令；新分支正在做全屏选择覆盖层。
- **桌面壳层**：`hide_main_window` / `restore_main_window` 已可用；共享缓冲区不适用于 Qt Widgets，等价用 `QPixmap` 内存对象替代。

## 二、UI 现状与前端参考的差异

1. **主界面骨架**：`TitleBar` 多了搜索框，缺昵称和完整窗口控制；`AppNav` 选中态是蓝色实心块；`MainWindow` 未按 `TitleBar + AppNav + QStackedWidget` 组织。
2. **消息页布局**：左侧仍是“我的好友/群聊/在线成员”树形列表，不是 `SessionList`（260px）+ `ChatPanel`。
3. **聊天面板**：
   - 输入栏太宽、灰色、工具栏 emoji 样式不对；
   - 缺少表情选择面板、`@` 提及补全；
   - 消息气泡颜色、圆角、头像、时间、状态、引用、转发、文件/图片预览未对齐；
   - 缺少消息和头像的右键菜单。
4. **其它视图**：`ContactsView`、`FavoritesView`、`SettingsView`、`ProfileView` 框架存在，但样式和空状态需统一。
5. **后端能力未接入**：本地消息操作、文件/图片另存为/Base64 预览、截图完整调用链、桌面壳层命令尚未全面接入 UI。
6. **主题**：暗色模式、QSS 全局样式未完善。

## 三、参考设计系统（直接复用）

来自 `tauri-qqnt/src/styles/theme.css`：

```css
--qq-bg: #ffffff; --qq-bg-secondary: #f5f6f7; --qq-bg-tertiary: #ebedf0;
--qq-border: #e1e3e6; --qq-text: #1f2329; --qq-text-secondary: #5f6672;
--qq-text-tertiary: #8f959e; --qq-primary: #0099ff; --qq-primary-hover: #007acc;
--qq-primary-soft: #e6f4ff; --qq-danger: #ff4d4f; --qq-warning: #faad14;
--qq-success: #52c41a; --qq-radius: 6px; --qq-titlebar-height: 40px;
--qq-sidebar-width: 64px; --qq-session-width: 260px;
```

字体：`PingFang SC`、`Microsoft YaHei`、`-apple-system`、`Segoe UI`。

## 四、分阶段实施计划

### 阶段 1：主界面骨架与导航（2-3 小时）

- `TitleBar`：移除搜索框，左侧 Q 图标 + “QQ NT” + 用户昵称；右侧最小化/最大化/关闭；支持鼠标拖拽移动无边框窗口。
- `AppNav`：选中态改为 `bg-primary-soft` + `text-primary`；mock 项加黄色小点；未读角标红色圆角标签。
- `MainWindow`：无边框窗口，根布局为 `TitleBar + AppNav + QStackedWidget`；尺寸 1100x740，最小 860x540。
- `ThemeManager`：把颜色变量接入 `QPalette` 和 QSS，建立 `themeChanged` 信号。

### 阶段 2：消息页布局（2-3 小时）

- 重构 `MessagesView`：`SessionList`（固定 260px）+ `ChatPanel`（flex-1）。
- `SessionList`：
  - 顶部标题栏“消息”。
  - 搜索框（调用 `GlobalSearchDialog`）。
  - 会话项：头像、名称、最后消息、时间、@标签、未读、置顶星标。
  - 空状态“暂无会话”。
- `ChatPanel`：
  - 标题栏：会话名称 + 在线状态 + 精华按钮（群聊）。
  - 中部 `MessageList`。
  - 底部 `ComposerWidget`。
  - 拖拽文件时显示高亮覆盖提示。
- 连接 `ChatSessionManager`/`ChatContextManager`/`ComposerManager`。

### 阶段 3：聊天面板细节（3-4 天）

- **消息气泡（ChatBubbleDelegate）**：
  - 自己：`#0099ff` 背景，白色文字，圆角 8-12px；
  - 对方：`#ebedf0` 背景，`#1f2329` 文字；
  - 头像 36px，fallback 首字母；同一人连续消息省略头像；时间戳、发送状态、系统消息居中。
  - 支持引用消息和转发标签。
- **文件/图片消息**：图片缩略圆角；文件卡片显示图标、文件名、大小、下载/打开文件夹按钮。
- **ComposerWidget**：
  - 工具栏图标统一 28px，hover 变主色。
  - 表情选择面板：10 分类（最近/超级/小黄脸/手势/爱心/动物/自然/食物/物品/符号）。
  - `@` 输入时弹出成员补全列表。
  - 占位文案：`发给 {sessionName}…（Enter 发送，Shift/Ctrl+Enter 换行，Esc 清空草稿）`。
  - 发送按钮主色，空内容置灰；被禁言时显示提示并禁用输入。
- **右键菜单**：
  - 消息：复制、转发、收藏/取消、多选、引用、精华/取消、撤回、删除。
  - 头像：发消息、@、查看资料、添加好友、修改群昵称、禁言/解除、设置/取消管理员、屏蔽、举报。

### 阶段 4：后端能力接入 UI（1-2 天）

- 将右键菜单和工具栏操作统一接到 `QQNTBackendService`：
  - 本地删除、收藏、表情、置顶备注、多选、引用、精华、撤回、转发。
  - 查看成员资料、举报、拉黑、设置群昵称。
- 本地操作记录读取与回显；多选模式底部操作栏（转发/删除/收藏）。
- 文件/图片：
  - 图片预览调用 `ImagePreviewWindow`（Base64 或本地路径）。
  - 另存为：调用 `QQNTBackendService` 保存文件。
  - 打开文件夹：打开文件所在目录并选中文件。
- 拖拽文件到聊天面板：通过 `ComposerManager` 发送文件/图片。

### 阶段 5：截图与桌面壳层（1-2 天）

- 完成 `ScreenshotCaptureWindow`：
  - 截图前隐藏主窗口，截图后恢复。
  - 全屏选择覆盖层支持框选、裁剪。
  - 跨显示器：用 `QScreen`/`QGuiApplication::screens()` 获取所有屏幕几何。
  - 排除自身窗口：覆盖层设为 `Qt::WindowStaysOnTopHint`，主窗口隐藏，必要时设置 `WS_EX_TOOLWINDOW` 样式。
  - 截图结果粘贴到 `Composer` 或保存为文件。
- 统一 `WindowShell` 调用：隐藏/恢复主窗口、用 `QPixmap` 替代共享缓冲区、系统默认打开文件/文件夹。

### 阶段 6：其它视图与弹窗（1-2 天）

- `ContactsView`：搜索 + 好友/群聊 Tab + 列表项。
- `FavoritesView`：空状态居中。
- `SettingsView`：主题、通知、快捷键、账号、关于。
- `ProfileView`：头像、昵称、账号、签名、二维码、编辑资料。
- `AddFriendDialog`、`CreateGroupDialog`、`GlobalSearchDialog`、`FriendManagerDialog`、
  `ImagePreviewWindow`、`ForwardWindow`、`RegisterWindow`、`NoticeFilterWindow` 等统一自定义标题栏和主题。

### 阶段 7：主题、QSS 与暗色模式（1-2 天）

- 完善 `style-qqnt.qss` 与 `style-qqnt-dark.qss`：
  - 全局字体、按钮、输入框、列表项、滚动条（6px）、消息气泡。
- 所有组件监听 `ThemeManager::themeChanged` 刷新。
- 暗色 token 与 `tauri-qqnt` 的 `.dark` 一致。

### 阶段 8：构建与截图验收（贯穿全程）

- 每阶段完成后用 `CMake` 和 `qmake` 构建。
- 每个视图截图保存到 `screenshots/`，与 `tauri-qqnt` 前端对应区域对比。
- 回归：登录/注册、收发消息、文件/图片、E2E、网络重连、主题切换。

## 五、关键文件清单

- UI 层：`src/widgets/titlebar.cpp`, `src/widgets/appnav.cpp`, `src/mainwindow.cpp`, `ui/mainwindow.ui`
- 视图：`src/views/messagesview.cpp`, `src/views/contactsview.cpp`, `src/views/favoritesview.cpp`, `src/views/settingsview.cpp`, `src/views/profileview.cpp`
- 聊天组件：`src/widgets/composerwidget.cpp`, `src/widgets/composerTextEdit.cpp`, `src/chatbubbledelegate.cpp`, `src/views/messagesview.cpp`
- 后端服务：`src/qqnt_backend_service.cpp`, `src/qqnt_engine_command_router.cpp`
- 截图与窗口：`src/windows/screenshotcapturewindow.cpp`, `src/windows/imagepreviewwindow.cpp`, `src/windows/forwardwindow.cpp`
- 主题：`src/theme/thememanager.cpp`
- 构建：`CMakeLists.txt`, `QtNetworkChat.pro`

## 六、验收标准

- 主界面、消息页、聊天面板、联系人/收藏/设置页截图与 `tauri-qqnt` 前端一致。
- 消息右键菜单和头像右键菜单的所有操作均调用 `QQNTBackendService` 或 `qqnt_engine_command_router`，UI 正确刷新。
- 多选模式可用，底部操作栏出现转发/删除/收藏按钮。
- 文件/图片可预览、另存为、打开文件夹；拖拽文件可发送。
- 截图覆盖层支持框选、裁剪、跨显示器、排除自身窗口。
- 亮/暗色切换即时生效；构建通过；业务逻辑不受影响。

## 七、下一步

待确认该合并计划后，切换到 **Code 模式**，从**阶段 1（TitleBar / AppNav）**开始逐步实现。每完成一个阶段截图验证，上下文满了会自动压缩历史。
