# 桌面、存储与生产 E2E 配置续修

日期：2026-10-04。本文补充上一轮拆分后的验证缺口；最终发布和 CI 状态以
[计划状态](plan-completion-status.md) 为准，不沿用旧的 54/55 或 55/55 数字。

## 生产配置的三个失败

`E2ECryptoBackendConfig` 原来写死 backend/adapter flags 为 0、请求为 none。
现在 CMake 将当前构建的预期值和原因传给脚本，验证生成配置是否匹配，既检查
字段存在也检查实际值；仅用 `[01]` 正则会漏掉“生成值与构建选项不一致”。

`E2ERolloutObservabilityEvidence` 现在区分未链接、链接但未就绪和已绑定 provider
的状态/gate/proof。生产报告的状态是 `ready`；任何配置都不允许导出原始密钥、
私密材料、会话 secret、明文或密文的敏感标志为 true。

`E2EPrivateMessageDelivery` 的默认场景包含大量“未链接/被阻止”的断言，不能
用它验证已链接构建。生产可用时现在运行已有的真实多 peer 轮换/迁移场景，
包含生产文本、文件、身份信任持久化和重启；关闭配置仍验证草案和 fail-closed
行为。三个生产测试已在本机通过；这不是通过删掉全部负例使测试变绿。

## 引擎好友事件的偶发失败

断言拆成发送端 ACK + `delivered=true` 和接收端 `request_received` 两阶段，每阶段
仍持续泵两端进程。原来的判断只确认 delivered 是 bool，误把 false 也当成功。
失败会输出 ACK、好友/错误事件、未完成 stdout 及 stderr。

本轮负载下曾复现一次发送端阶段失败，而 Bob 日志已有 friend_request，说明
“Bob 未收到”和“事件泵错过窗口”不是已经确定的根因。加诊断后的生产构建连续
20 次和 40 次通过；默认构建此前 12 次通过。复跑结果不等于已经证明所有环境
下不再偶发，也不能把延长超时当成根因修复。

## GUI 和原生选择器

`MainWindowExtendedGui` 覆盖创建菜单的 23 个命令、当前菜单栏动作、好友拒绝/
接受、群申请拒绝/批准、截图拖选/确认/取消，以及实际 2 MiB 上传的取消和
重新选择同一文件发送，接收结果逐字节比较。后者是重发，不是同 transferId
的断点恢复；恢复协议有独立专项。

另覆盖当前 MessagesView 的消息右键动作、复制/引用的效果、确认删除，以及
GroupMemberSidebar 所有菜单/禁言时长/管理子菜单的参数分发。这些成员菜单
检查证明 GUI 接线，网络权限/持久化由群协议和真实数据库测试验证，不能把
纯信号分发称为每个菜单都已通过完整服务器变更。

原生场景不设置 AA_DontUseNativeDialogs，单独执行取消文件选择、选择文件并
核对接收字节、上传头像、修改目录和导出历史。OS 输入只操作本测试进程的
原生窗口，轮询线程在派生对象字段销毁前停止并 join。Linux 用 GTK/X11/Xvfb；
Windows 使用 Win32 文件对话框。原生 modal loop 可能暂停 Qt 定时器，因此
OS 输入轮询使用独立线程。Linux 五项已通过，Windows 结果等对应 Actions。

`.bin` 不在普通文件选择器默认的“常用文件”过滤器中，测试需要先选择“所有
文件”；否则 Open 被禁用。文件模型异步加载也需等待按钮可用，不能排队
调用 accept 后就认为选择成功。

## TLS 与真实服务

`TlsLoginFrameOrdering` 用真实本地 TLS 服务分别验证 TCP/TLS 和 WSS 的正确
pin、错误 pin、未知 CA、可信 CA、错误主机名。错误场景核对服务器收到零个
登录帧；正确场景核对恰好一个登录帧和登录成功。TCP fixture 在单独 QThread，
避免 Client 同步握手与同线程测试服务互相等待。

`RealRedisCrossInstanceRouting` 在实际 Redis 上运行两个 Server，验证在线状态、
Pub/Sub、群/私聊、普通/对象大文件、回执清理、断线 presence 和换实例离线重放。
本机 Redis 8.0.5 的专项已通过。Linux CI 使用隔离 PostgreSQL16/Redis7，实际
执行 QPSQL 完整协议专项和真实跨实例 Redis 测试，不接触生产库。

未设置真实服务测试开关时返回 77，CTest 明确显示 skipped。运行包装补充 Linux
使用的 TMPDIR：此前只设置 TMP/TEMP，导致本地消息动作在 Linux 共用临时
目录，出现收藏数据污染。QPSQL fixture 使用 QTemporaryDir 管理本次文件，
不递归删除调用方提供的目录。

## 复验

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build-production-check --output-on-failure \
  -R '^(E2ECryptoBackendConfig|E2ERolloutObservabilityEvidence|E2EPrivateMessageDelivery|MainWindowExtendedGui|QQNTBackendLocalActions|TlsLoginFrameOrdering)$'
QT_QPA_PLATFORM=offscreen ctest --test-dir build-production-check --output-on-failure \
  -R '^QQNTEngineEndToEnd$' --repeat until-fail:20
xvfb-run -a env GDK_BACKEND=x11 QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=gtk3 \
  QTNETWORKCHAT_NATIVE_DIALOG_TEST=1 QTNETWORKCHAT_NATIVE_DIALOG_AUTOMATE=1 \
  build-production-check/mainwindow_extended_gui_test nativePickers
```

完整默认/生产 suite、真实 PostgreSQL CI、两平台原生验证和最新 exe/deb 的最终
结果必须在完成后填写计划状态。Windows 安装程序仍未签名；没有独立密码学
审计；未验证所有 OS 通知策略和多显示器组合。
