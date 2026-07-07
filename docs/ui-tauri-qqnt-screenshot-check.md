# QtNetworkChat 对照 tauri-qqnt 截图检查

检查时间：2026-07-07  
Qt 仓库：`D:\C++VS pro\QtNetworkChat`  
对照仓库：`D:\C++VS pro\tauri-qqnt`  
当前分支：`codex/qqnt-tauri-backend-sync`

## 截图产物

| 场景 | tauri-qqnt 截图 | QtNetworkChat 截图 | 结论 |
| --- | --- | --- | --- |
| 登录 / 入口 | `D:\C++VS pro\QtNetworkChat\docs\screenshots\tauri-login.png` | `D:\C++VS pro\QtNetworkChat\docs\screenshots\qt-login.png` | 已做 QQ 风格入口：渐变/头像/服务状态/注册登录入口；Qt 入口保留本地服务/Redis 状态，符合 Qt 仓库定位。 |
| 主聊天布局 | `D:\C++VS pro\QtNetworkChat\docs\screenshots\tauri-main.png` | `D:\C++VS pro\QtNetworkChat\docs\screenshots\qt-main.png` | 已做三栏/四区布局：导航、会话、聊天、群成员；Qt 没有照搬 React/Tauri，但视觉结构和核心功能区已对齐。 |
| 群成员面板 | `D:\C++VS pro\QtNetworkChat\docs\screenshots\tauri-group-members.png` | `D:\C++VS pro\QtNetworkChat\docs\screenshots\qt-group-members.png` | 已做：头像、昵称、角色 badge、在线状态、QQ 号、禁言时间；对应 `GroupMemberItemDelegate`。 |
| 消息右键菜单 | `D:\C++VS pro\QtNetworkChat\docs\screenshots\tauri-message-context-menu.png` | `D:\C++VS pro\QtNetworkChat\docs\screenshots\qt-message-context-menu.png` | 已做核心项：收藏/取消收藏、多选/取消多选、设为精华/取消精华、撤回、删除、转发/复制等。 |
| 收藏页 | `D:\C++VS pro\QtNetworkChat\docs\screenshots\tauri-favorites.png` | Qt 主界面菜单/右键入口验证 | tauri 有独立收藏视图；Qt 当前是菜单与后端同步入口，不是完整独立收藏页。后端协议已覆盖收藏快照/更新。 |

## 对照结论

### 已同步/已做

1. **UI 结构已接近 tauri-qqnt**
   - 左侧导航按钮。
   - 会话列表 delegate。
   - 聊天气泡 delegate。
   - 群成员右侧栏。
   - 精华入口。
   - 文件/图片/截图/表情工具按钮。

2. **群成员截图要求已做**
   - `D:\C++VS pro\QtNetworkChat\include\groupmemberitemdelegate.h`
   - `D:\C++VS pro\QtNetworkChat\src\groupmemberitemdelegate.cpp`
   - 支持角色：群主、管理员、好友、成员。
   - 支持在线/离线状态。
   - 支持 QQ 号展示。
   - 支持禁言至时间展示。
   - 支持浅色/深色主题判断。

3. **右键菜单功能入口已做**
   - 消息：收藏、取消收藏、多选、取消多选、设为精华、取消精华、撤回群消息。
   - 群成员：查看资料、禁言 10 分钟、解除禁言、设置/取消管理员、移出群聊。

4. **后端能力与 UI 对应关系已验证**
   - 收藏：`set_message_favorite` / `message_favorite_updated` / `favorite_messages_snapshot`。
   - 精华：`set_group_essence_message` / `group_essence_updated`。
   - 群成员管理：`update_group_member` / `mute_group_member` / `unmute_group_member` / `get_group_member_profile`。
   - 公共群消息/文件/图片：Qt 已走服务端 group 路径。

### 仍不完全一样的地方

1. **Qt 不是 Tauri/React 仓库**
   - 没有同步 `package.json`、`src-tauri`、React view 组件，这是正确的。
   - Qt 使用 `QWidget + QSS + QStyledItemDelegate` 表达同类界面。

2. **收藏页不是完全同屏独立页**
   - tauri-qqnt 有独立 FavoritesView。
   - Qt 目前重点是收藏后端/右键入口/菜单操作；如果要“完全像 tauri 独立收藏页”，还需要新增 Qt 收藏列表页面。

3. **截图/图片预览视觉存在差异**
   - tauri 主界面里图片消息区域是 React 卡片。
   - Qt 当前 delegate 支持媒体预览角色，但主截图 harness 只验证了文字气泡和状态标签；真实图片预览需要实际发送图片后再截运行态。

## 本次截图方法

- tauri-qqnt：启动 Vite dev server 后用本机 Chrome + Playwright 截图。
- QtNetworkChat：使用不提交的临时 Qt harness，直接编译并调用当前仓库的：
  - `D:\C++VS pro\QtNetworkChat\ui\style.qss`
  - `D:\C++VS pro\QtNetworkChat\src\sessionitemdelegate.cpp`
  - `D:\C++VS pro\QtNetworkChat\src\chatbubbledelegate.cpp`
  - `D:\C++VS pro\QtNetworkChat\src\groupmemberitemdelegate.cpp`

## 结论

按“Qt 仓库不能变成 Tauri 仓库，但 UI/mainwindow.ui、ui/style.qss、Qt delegate 要对照覆盖”的标准：**截图对应功能已经做了**。

如果下一步要继续补齐，建议只补 Qt 自身页面：

1. Qt 独立“收藏消息”页面。
2. Qt 独立“精华消息”抽屉/弹窗。
3. 真机运行态图片消息截图，而不是 harness 渲染。
