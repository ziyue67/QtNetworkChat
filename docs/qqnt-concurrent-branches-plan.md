
# QQ NT 前后端并发分支任务设计

本方案把当前计划拆成 **两条可并行的 Git 大分支**：

- `codex/qqnt-backend`：C++ 引擎/服务端 + Rust Tauri 主进程 + 构建/测试/部署
- `codex/qqnt-frontend`：React 前端 + UI/UX + 状态管理 + 前端测试

**先有一条公共基线 `codex/qqnt-base`**，由后端负责搭建 Tauri 空壳（含最简 React 骨架），之后两条大分支由此分出独立推进。

---

## 1. 公共基线（必须先做）

### 基线分支：`codex/qqnt-base`
**负责人**：GPT5.5  
**目的**：让前后端有共同的目录、Tauri 配置和空 React 入口，避免后续在 `package.json` / `tauri.conf.json` 上冲突。

### 基线交付物

| 文件/目录 | 内容 |
|---|---|
| `tauri-qqnt/package.json` | 已含 React + TS + Vite + Tailwind 依赖占位；前端后续可以升级版本号，但保持脚本名不变 |
| `tauri-qqnt/vite.config.ts` | Vite + React plugin + `@/` alias 到 `src/` |
| `tauri-qqnt/tsconfig.json` | 标准 React TS 配置 |
| `tauri-qqnt/tailwind.config.js` | 空配置，前端负责填充 QQ NT 主题 |
| `tauri-qqnt/index.html` | 标准入口 |
| `tauri-qqnt/src/main.tsx` | 仅渲染 `<App />`；前端负责填充业务 |
| `tauri-qqnt/src/App.tsx` | 空壳 |
| `tauri-qqnt/src-tauri/` | Tauri v2 完整骨架 |
| `tauri-qqnt/src-tauri/Cargo.toml` | 依赖已包含 `tauri`、`tauri-plugin-shell`、`tokio`、`serde_json` 等 |
| `tauri-qqnt/src-tauri/tauri.conf.json` | `externalBin: ["binaries/QQNTEngine", "binaries/QQNTServer"]` 已注册；窗口 `decorations: false` |
| `tauri-qqnt/src-tauri/src/main.rs` | 调用 `tauri_qqnt_lib::run()` 进入 Rust 主进程 |
| `tauri-qqnt/src-tauri/src/lib.rs` | 初始化 shell/opener plugin，启动 `QQNTServer` 与 `QQNTEngine` sidecar，并注册 Tauri commands |
| `tauri-qqnt/src-tauri/capabilities/default.json` | 默认 capability |
| `scripts/copy-sidecars.ps1` | 将 `QQNTEngine` / `QQNTServer` 复制到 `tauri-qqnt/src-tauri/binaries/` 的 sidecar 同步脚本 |
| `dev/redis-compose.yml` | Redis compose |
| `docs/qqnt-ipcv1.md` | 协议 v1 文档框架 |

### 基线验收
```powershell
cd tauri-qqnt
npm install
npm run tauri dev
```
能打开一个**无边框空窗口**即算完成。

---

## 2. 大分支一：后端分支 `codex/qqnt-backend`

### 2.1 负责人与范围

**负责人**：GPT5.5  
**工作区**（只有后端分支可修改，前端分支不能碰）：

```
include/qqnt_*.h
src/qqnt_*.cpp
tools/qqnt_engine.cpp
tools/qqnt_server.cpp
CMakeLists.txt
scripts/copy-sidecars.ps1
tests/qqnt_*.cpp
tests/CMakeLists.txt
tests/fixtures/
docs/qqnt-ipcv1.md
dev/redis-compose.yml
tauri-qqnt/src-tauri/
```

### 2.2 后端目标

| 目标 | 说明 |
|---|---|
| `QQNTEngine.exe` | 无头 C++ Client 引擎，NDJSON 与 Rust 通信 |
| `QQNTServer.exe` | 无头 C++ Server，唯一连接 Redis |
| Rust 桥接 | Tauri 主进程中 spawn sidecar、读写 NDJSON、转发事件、处理命令 |
| 构建与打包 | CMake 编译 sidecar、自动同步到 Tauri bundle、Tauri 打包 |
| 协议文档 | 维护 `docs/qqnt-ipcv1.md` 作为前后端契约 |
| 测试 | CTest + Rust 集成测试 |

### 2.3 后端 WBS

| 编号 | 任务 | 输出文件 | 验收标准 |
|---|---|---|---|
| B0 | 基线已完成 | `codex/qqnt-base` | 见基线验收 |
| B1 | CMake 目标改造 | `CMakeLists.txt` | `cmake --build build-qt6-mingw --target QQNTEngine QQNTServer` 成功 |
| B2 | sidecar 同步脚本 | `scripts/copy-sidecars.ps1` | 编译后自动拷贝 exe 到 `tauri-qqnt/src-tauri/binaries/` |
| B3 | QQNTEngine 入口 + 日志隔离 | `tools/qqnt_engine.cpp` | `stdout` 仅含 NDJSON；`stderr` 输出日志 |
| B4 | NDJSON 桥接核心 | `src/qqnt_client_bridge.cpp` `include/qqnt_client_bridge.h` | 能读写 NDJSON；命令与事件格式正确 |
| B5 | 命令路由 | `src/qqnt_engine_command_router.cpp` `include/qqnt_engine_command_router.h` | `ready/connect/login/send_private_message` 可用 |
| B6 | Client signal 转事件 | `src/qqnt_client_bridge.cpp` | `message/file_progress/connection_state` 等事件发出 |
| B7 | QQNTServer 入口 | `tools/qqnt_server.cpp` | 服务端能启动 |
| B8 | Redis 服务封装 | `src/qqnt_redis_service.cpp` `include/qqnt_redis_service.h` | Server Redis 逻辑迁移完成 |
| B9 | Tauri Rust 主进程 | `tauri-qqnt/src-tauri/src/main.rs` `commands.rs` `bridge.rs` `sidecar.rs` `state.rs` `error.rs` | 能 spawn engine、读写 NDJSON、emit 事件 |
| B10 | Redis 预检与错误广播 | `tauri-qqnt/src-tauri/src/sidecar.rs` | Redis 未启动时 emit `qqnt://server/fatal` |
| B11 | Engine smoke 测试 | `tests/qqnt_engine_smoke_test.cpp` | 测试通过；stdout 每行合法 JSON |
| B12 | Server Redis 测试 | `tests/qqnt_server_redis_test.cpp` | Redis present + 跨实例消息通过 |
| B13 | Protocol drift 测试 | `tests/qqnt_protocol_drift_test.cpp` `tests/fixtures/ready.json` | 三端 fixture 解析通过 |
| B14 | Tauri 打包 | `tauri-qqnt/src-tauri/tauri.conf.json` | `npm run tauri build` 成功 |

### 2.4 后端里程碑

| 阶段 | 任务 | 验收 |
|---|---|---|
| BM1 | B1–B3 | `QQNTEngine.exe` 能启动，`stdout` 只有 NDJSON |
| BM2 | B4–B6 | Rust 能收到 `qqnt://engine/ready` 事件 |
| BM3 | B7–B8 | `QQNTServer.exe` Redis 就绪 |
| BM4 | B9–B10 | 前端/Rust 能 login、connect、收到 message |
| BM5 | B11–B13 | CTest 全部通过 |
| BM6 | B14 | MSI/NSIS 打包可用 |

### 2.5 后端给前端提供的约定

- 事件命名不变：`qqnt://engine/<event>`
- 命令入口不变：`invoke('qqnt_command', payload)`
- 协议文档 `docs/qqnt-ipcv1.md` 由后端维护，前端阅读
- `ready` 事件固定返回 `protocolVersion: 1`
- `message` 事件 payload 至少包含：
  ```json
  {
    "sessionId": "peerId 或 group:<groupId>",
    "message": { ... }
  }
  ```

---

## 3. 大分支二：前端分支 `codex/qqnt-frontend`

### 3.1 负责人与范围

**负责人**：Kimi  
**工作区**（只有前端分支可修改，后端分支不能碰）：

```
tauri-qqnt/src/
tauri-qqnt/package.json        # 版本/依赖升级
tauri-qqnt/vite.config.ts
tauri-qqnt/tailwind.config.js
tauri-qqnt/tsconfig.json
tauri-qqnt/index.html
```

**共享只读区**（前端可读、建议不修改；如需改动提给后端）：

```
tauri-qqnt/src-tauri/tauri.conf.json   # 窗口配置、externalBin
docs/qqnt-ipcv1.md                     # 协议契约
```

### 3.2 前端目标

| 目标 | 说明 |
|---|---|
| QQ NT 视觉 | 无边框窗口、侧边栏、三栏布局、QQ NT 色板 |
| 状态管理 | Zustand 分 store |
| 消息模块 | 会话列表、聊天面板、气泡、输入框、图片/文件 |
| 联系人模块 | 好友/群聊列表、加好友、建群 |
| 设置/扩展 | 设置页、个人资料、8 个 mock 扩展入口 |
| 前端测试 | Vitest + React Testing Library |
| 联调 | 与后端分支合并后验证端到端 |

### 3.3 前端 WBS

| 编号 | 任务 | 输出文件 | 验收标准 |
|---|---|---|---|
| F0 | 基于基线拉分支 | `codex/qqnt-frontend` | 从 `codex/qqnt-base` 切出，能 dev |
| F1 | 主题与全局样式 | `tauri-qqnt/src/styles/theme.css` `tailwind.config.js` | 色板符合 QQ NT |
| F2 | 无边框窗口组件 | `components/frame/TitleBar.tsx` `WindowControls.tsx` | 拖动、最小化/最大化/关闭正常 |
| F3 | 导航入口 | `components/sidebar/AppNav.tsx` `NavItem.tsx` `config/appEntries.ts` | 11 个入口可切换 |
| F4 | 布局框架 | `views/MainLayout.tsx` | 三栏布局自适应 |
| F5 | 核心类型 | `types/qqnt.ts` | 类型覆盖 user/session/message/contact/group/file |
| F6 | Zustand stores | `stores/authStore.ts` `contactStore.ts` `sessionStore.ts` `messageStore.ts` `fileStore.ts` `uiStore.ts` | 组件可读写状态 |
| F7 | IPC hooks | `hooks/useQQNTEvents.ts` `useEngine.ts` | 监听 engine 事件；调用 commands |
| F8 | API 封装 | `api/qqnt.ts` | 所有命令函数已声明 |
| F9 | 登录页 | `views/LoginView.tsx` | 可输入账号密码、服务器地址、托管开关 |
| F10 | 会话列表 | `components/session/SessionList.tsx` `SessionItem.tsx` | 显示未读、最后消息、时间 |
| F11 | 聊天面板 | `components/chat/ChatPanel.tsx` `MessageList.tsx` `MessageBubble.tsx` `Composer.tsx` | 能发文本、显示气泡 |
| F12 | 文件消息 | `components/chat/FileMessage.tsx` | 显示进度、文件名、状态 |
| F13 | 联系人视图 | `views/ContactsView.tsx` `components/contact/ContactCard.tsx` `AddFriendModal.tsx` | 好友/群聊分组 |
| F14 | 设置与个人资料 | `views/SettingsView.tsx` `components/profile/ProfileCard.tsx` | 设置项 UI 完整 |
| F15 | 扩展页 mock | `views/SpaceView.tsx` ... `WalletView.tsx` | 8 个入口有占位 |
| F16 | 虚拟滚动 | `components/chat/MessageList.tsx` | 1 万条消息不卡顿 |
| F17 | 前端单元测试 | `*.test.ts` `*.test.tsx` | Vitest 通过 |
| F18 | 联调与集成 | 合并 `codex/qqnt-backend` | 端到端消息、文件、登录通过 |

### 3.4 前端里程碑

| 阶段 | 任务 | 验收 |
|---|---|---|
| FM1 | F1–F4 | 打开无边框 QQ NT 主窗口，11 入口可切换 |
| FM2 | F5–F8 | stores 与 IPC 框架ready，mock engine 事件能驱动 UI |
| FM3 | F9–F12 | 登录界面 + 聊天界面可用（可用 mock 数据跑 UI） |
| FM4 | F13–F15 | 联系人/设置/扩展页完整 |
| FM5 | F16–F17 | 性能 + 测试通过 |
| FM6 | F18 | 与后端分支合并后真实链路跑通 |

### 3.5 前端给后端的约定

- 命令 `reqId` 使用 `crypto.randomUUID()` 生成
- 调用统一走 `invoke('qqnt_command', { payload: {...} })`
- 监听事件统一 `listen('qqnt://engine/<event>', ...)`
- `types/qqnt.ts` 与 `docs/qqnt-ipcv1.md` 对齐；若需新增字段，先更新文档再通知后端

---

## 4. 并行开发的冲突避免规则

### 4.1 文件所有权

| 区域 | 后端分支 | 前端分支 |
|---|---|---|
| `tauri-qqnt/src/**/*` | ❌ 不修改 | ✅ 全权负责 |
| `tauri-qqnt/src-tauri/**/*` | ✅ 全权负责 | ❌ 不修改 |
| `tauri-qqnt/package.json` | 可新增脚本/依赖 | 可新增脚本/依赖 |
| `tauri-qqnt/src-tauri/tauri.conf.json` | 窗口、externalBin 配置 | 只读 |
| `include/qqnt_*`、`src/qqnt_*`、`tools/qqnt_*` | ✅ 全权负责 | ❌ 不修改 |
| `CMakeLists.txt`、`tests/qqnt_*` | ✅ 全权负责 | ❌ 不修改 |
| `docs/qqnt-ipcv1.md` | 维护协议 | 只读 |
| `dev/redis-compose.yml`、`scripts/copy-sidecars.ps1` | ✅ 全权负责 | ❌ 不修改 |

### 4.2 共享变更流程

两端共享的变更必须走**协议文档先行**：

1. 需要新增命令/事件/字段时，先在 `docs/qqnt-ipcv1.md` 的 PR（或口头同步）里定义。
2. 后端更新 C++ 与 Rust 实现。
3. 前端更新 `types/qqnt.ts` 与 `api/qqnt.ts`。
4. 双方更新各自的测试。

### 4.3 每日集成检查点

| 时间点 | 动作 |
|---|---|
| 每天下班前 | 各自分支 `git push`；另一方 `git fetch` 查看最新变更 |
| 每周三/五 | 把 `codex/qqnt-base` 的更新 rebase 进自己分支 |
| BM2 / FM2 完成时 | 合并一次中间版本，验证 `ready` 事件能驱动前端 UI |
| 后端新增命令后 | 前端在 24h 内补齐对应 API 函数与 types |

---

## 5. 执行顺序（总 timeline）

```
Day 0: 从 main 切出 codex/qqnt-base，后端完成基线
        ↓
Day 1: 从 base 同时切出 codex/qqnt-backend 与 codex/qqnt-frontend
        ↓
Week 1: 后端 BM1-BM2；前端 FM1-FM2
        ↓
Week 2: 后端 BM3-BM4；前端 FM3-FM4
        ↓
Week 3: 后端 BM5；前端 FM5；中间联调
        ↓
Week 4: 合并两个大分支 → main；后端 BM6；前端 FM6
        ↓
发布前: 全量测试 + Tauri 打包
```

---

## 6. 验收与合并定义

### 各自分支可独立合并条件

- **后端分支合入 base/main 前**：
  - `cmake --build build-qt6-mingw --target QQNTEngine QQNTServer` 成功
  - `ctest --test-dir build-qt6-mingw --output-on-failure` 通过（含新增测试）
  - `npm run tauri build` 在 `tauri-qqnt/` 下通过
- **前端分支合入 base/main 前**：
  - `npm install && npm run tauri dev` 成功
  - `npm run test` 通过
  - 与后端合并后的中间分支跑通登录 + 私聊 + 文件发送

### 最终合并分支建议

```
main
└── codex/qqnt-base          （基线，后端搭建）
    ├── codex/qqnt-backend   ← 后端大分支
    └── codex/qqnt-frontend  ← 前端大分支
           ↓
    codex/qqnt-integration   ← 合并后端+前端，做联调
           ↓
        main                 ← 最终合并
```

---

## 7. 立即开始动作

1. **后端（GPT5.5）**：
   - `git checkout -b codex/qqnt-base`
   - 跑 `npm create tauri-app@latest tauri-qqnt -- --template react-ts`
   - 配置 `externalBin`、Rust 主进程入口、`scripts/copy-sidecars.ps1`、`dev/redis-compose.yml`、`docs/qqnt-ipcv1.md`
   - 基线验收 `npm run tauri dev` 能打开无边框窗口
   - push 后切 `codex/qqnt-backend` 开始 B 任务

2. **前端（Kimi）**：
   - 等基线 push 后 `git checkout -b codex/qqnt-frontend codex/qqnt-base`
   - 开始 F1–F4 主题/窗口/导航/布局

