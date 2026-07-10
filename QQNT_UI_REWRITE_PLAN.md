# QQNT UI 完整重写计划 — Qt C++ 版本

## 摘要

将 Tauri/React 前端的 QQNT 设计系统完整移植到 Qt C++ 版本。当前 Qt 版本使用原生 QMainWindow + 传统三栏布局（左侧面板/中间聊天/右侧信息），需要重写为 QQNT 风格：自定义标题栏 + 左侧图标导航(AppNav) + 会话列表 + 聊天面板 + 底部输入栏(Composer)。

## 关键设计系统（从 Tauri 前端提取）

### 颜色变量
| 变量 | 亮色模式 | 暗色模式 |
|------|---------|---------|
| `--qq-bg` | `#ffffff` | `#1e1e1e` |
| `--qq-bg-secondary` | `#f5f6f7` | `#252525` |
| `--qq-bg-tertiary` | `#ebedf0` | `#2d2d2d` |
| `--qq-border` | `#e1e3e6` | `#3a3a3a` |
| `--qq-text` | `#1f2329` | `#e8e8e8` |
| `--qq-text-secondary` | `#5f6672` | `#a8a8a8` |
| `--qq-text-tertiary` | `#8f959e` | `#787878` |
| `--qq-primary` | `#0099ff` | `#3da7ff` |
| `--qq-primary-hover` | `#007acc` | `#66c1ff` |
| `--qq-primary-soft` | `#e6f4ff` | `#1a3a52` |
| `--qq-danger` | `#ff4d4f` | `#ff7875` |
| `--qq-success` | `#52c41a` | `#73d13d` |

### 尺寸变量
- 标题栏高度: `40px`
- 侧边栏宽度: `64px`
- 会话列表宽度: `260px`
- 圆角: `6px`
- 阴影: `0 2px 12px rgba(0,0,0,0.08)`

### 布局结构
```
MainWindow (无边框窗口)
├── TitleBar (自定义标题栏, 40px高, 可拖拽)
│   ├── Logo (Q图标)
│   ├── 标题 ("QQ NT")
│   ├── 用户信息 (昵称)
│   └── WindowControls (最小化/最大化/关闭)
├── 主内容区 (flex-1)
│   ├── AppNav (左侧图标导航, 64px宽)
│   │   ├── 消息 (MessageIcon)
│   │   ├── 联系人 (ContactsIcon)
│   │   ├── 空间 (SpaceIcon) [mock]
│   │   ├── 频道 (ChannelIcon) [mock]
│   │   ├── 邮件 (MailIcon) [mock]
│   │   ├── 文档 (DocsIcon) [mock]
│   │   ├── 日历 (CalendarIcon) [mock]
│   │   ├── 会议 (MeetingIcon) [mock]
│   │   ├── 收藏 (FavoritesIcon) [mock]
│   │   ├── 钱包 (WalletIcon) [mock]
│   │   └── 设置 (SettingsIcon)
│   └── 内容区 (根据路由切换)
│       ├── MessageView (消息视图)
│       │   ├── SessionList (会话列表, 260px宽)
│       │   └── ChatPanel (聊天面板)
│       │       ├── ChatHeader (聊天标题栏)
│       │       ├── MessageList (消息列表)
│       │       └── Composer (底部输入栏)
│       ├── ContactsView (联系人视图)
│       ├── SettingsView (设置视图)
│       └── ProfileView (个人资料视图)
```

## 实现变更

### 1. 新组件（widgets/ 目录）

#### `TitleBar` (`include/widgets/titlebar.h`, `src/widgets/titlebar.cpp`)
- 继承 `QFrame`
- 高度 40px，无边框
- 左侧: QQ Logo (圆形蓝色背景 + 白色"Q") + "QQ NT" 标题 + 用户昵称
- 右侧: 窗口控制按钮 (最小化/最大化/关闭)
- 支持鼠标拖拽移动窗口
- 背景色: `--qq-bg-secondary`

#### `AppNav` (`include/widgets/appnav.h`, `src/widgets/appnav.cpp`)
- 继承 `QFrame`
- 宽度 64px，垂直布局
- 11 个导航项（图标 + 标签）
- 当前项高亮: 蓝色背景 + 白色图标
- 未读消息角标 (红色圆点)
- mock 项显示灰色图标
- 点击切换路由信号 `routeActivated(QString)`

#### `AvatarLabel` (`include/widgets/avatarlabel.h`, `src/widgets/avatarlabel.cpp`)
- 继承 `QLabel`
- 圆形头像显示
- 支持在线状态指示器 (小圆点)
- 支持显示昵称首字母作为 fallback

#### `ComposerWidget` (`include/widgets/composerwidget.h`, `src/widgets/composerwidget.cpp`)
- 继承 `QFrame`
- 底部输入栏，包含:
  - 工具栏: 表情/图片/文件/截图/历史记录按钮
  - 文本输入区 ( QTextEdit )
  - 发送按钮
- 支持 @提及 自动补全
- 支持表情选择面板
- 支持拖拽文件

### 2. 新视图（views/ 目录）

#### `MessagesView` (`include/views/messagesview.h`, `src/views/messagesview.cpp`)
- 包含 SessionList + ChatPanel
- 管理会话状态和消息显示

#### `ContactsView` (`include/views/contactsview.h`, `src/views/contactsview.cpp`)
- 联系人列表
- 好友/群组分类

#### `SettingsView` (`include/views/settingsview.h`, `src/views/settingsview.cpp`)
- 设置面板
- 主题切换/通知/快捷键

#### `ProfileView` (`include/views/profileview.h`, `src/views/profileview.cpp`)
- 个人资料展示

#### `FavoritesView` (`include/views/favoritesview.h`, `src/views/favoritesview.cpp`)
- 收藏消息列表 (mock)

### 3. 新对话框（dialogs/ 目录）

- `AddFriendDialog` - 添加好友
- `CreateGroupDialog` - 创建群聊
- `GlobalSearchDialog` - 全局搜索
- `FriendManagerDialog` - 好友管理器
- `MuteDurationDialog` - 禁言时长
- `GroupNicknameDialog` - 群昵称
- `MemberProfileCard` - 成员资料卡
- `EssencePanel` - 精华消息面板

### 4. 新窗口（windows/ 目录）

- `ImagePreviewWindow` - 图片预览
- `RegisterWindow` - 注册窗口
- `ForwardWindow` - 转发窗口
- `ScreenshotCaptureWindow` - 截图窗口
- `NoticeFilterWindow` - 通知筛选

### 5. 主题系统（theme/ 目录）

#### `ThemeManager` (`include/theme/thememanager.h`, `src/theme/thememanager.cpp`)
- 单例模式
- 管理亮色/暗色主题
- 提供 `color(QString key)` 方法
- 发射 `themeChanged()` 信号
- 所有组件连接此信号更新样式

### 6. 主窗口重构

#### `MainWindow` 修改
- 移除 `QMainWindow` 的菜单栏和状态栏
- 使用无边框窗口 (`Qt::FramelessWindowHint`)
- 新布局:
  ```
  QVBoxLayout (root)
  ├── TitleBar
  └── QHBoxLayout (content)
      ├── AppNav
      └── QStackedWidget (views)
          ├── MessagesView
          ├── ContactsView
          ├── FavoritesView
          ├── SettingsView
          └── ProfileView
  ```
- 路由管理: `AppNav` 点击切换 `QStackedWidget` 当前页
- 登录窗口独立为 `LoginWindow`（无边框 + 渐变背景）

### 7. QSS 样式系统

新建 `ui/style-qqnt.qss` 替换现有样式:
- 全局字体: PingFang SC / Microsoft YaHei
- 按钮圆角 6px
- 输入框边框 1px `#e1e3e6`
- 列表项 hover 效果
- 滚动条 6px 宽
- 消息气泡样式（左/右对齐）

## 测试计划

1. **构建测试**: 确保所有新文件正确添加到 .pro 和 CMakeLists.txt
2. **启动测试**: 登录窗口显示 QQNT 风格
3. **导航测试**: AppNav 切换各视图
4. **聊天测试**: 发送/接收消息，消息气泡显示
5. **主题测试**: 亮色/暗色切换
6. **截图测试**: 每个视图截图保存到 `screenshots/`

## 假设

- 保留现有所有业务逻辑（Client/Server/E2E/Transfer等）不变
- 仅修改 UI 层（mainwindow.cpp/ui/ + 新增组件）
- 当前 `main` 分支 (`ec6b0ad3`) 作为基础
- 使用 Qt 6.8.3 + MinGW 构建
