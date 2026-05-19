# QtNetworkChat

基于 Qt5/Qt6 + C++ 的局域网即时通讯工具，支持群聊、私聊、文件传输。

## 功能特性

- TCP Socket 通信
- 群聊广播消息
- 私聊功能（双击用户列表）
- 文件传输（Base64 编码）
- 心跳保活机制
- 用户在线列表
- 聊天记录持久化
- 系统托盘支持
- 自动重连

## 项目结构

```
QtNetworkChat/
├── include/
│   ├── mainwindow.h    # 主窗口
│   ├── server.h        # TCP 服务器
│   ├── client.h        # TCP 客户端
│   ├── message.h      # 消息结构
│   └── chatuser.h     # 用户结构
├── src/
│   ├── main.cpp        # 程序入口
│   ├── mainwindow.cpp  # 主窗口实现
│   ├── server.cpp      # 服务器实现
│   ├── client.cpp      # 客户端实现
│   └── message.cpp     # 消息序列化
├── ui/
│   └── mainwindow.ui   # Qt Designer 界面
├── QtNetworkChat.pro   # qmake 项目文件
└── CMakeLists.txt     # CMake 构建文件
```

## 构建方法

### qmake（推荐）
```bash
cd QtNetworkChat
qmake QtNetworkChat.pro
make
# Windows: nmake 或 jom
# macOS: make
# Linux: make
```

### CMake
```bash
mkdir build && cd build
cmake ..
make
```

## 运行

```bash
./bin/QtNetworkChat
```

## 使用方法

1. **创建服务器**: 点击"创建服务器"按钮，输入用户名和端口（默认 8888）
2. **加入服务器**: 点击"加入服务器"按钮，输入服务器 IP、端口和用户名
3. **群聊**: 直接在输入框发送消息，所有在线用户都能收到
4. **私聊**: 双击用户列表中的用户，再发送消息，仅对方可见
5. **发送文件**: 点击"发送文件"按钮，选择文件后发送

## 技术栈

- Qt5/Qt6
- C++17
- TCP Socket
- QJson
- QSettings

## 系统要求

- Qt 5.15+ 或 Qt 6.x
- C++17 兼容编译器
- 支持 TCP 的操作系统（Windows/macOS/Linux）
