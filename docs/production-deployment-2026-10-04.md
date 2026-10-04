# 2026-10-04 生产容器升级与验证

本次已实际登录服务器并完成 QtNetworkChat 服务端升级。运行的业务源码来自
`3eeb955b25a0a18d174102c4bd2c31195ebcea5e`；桌面安装包仍为 `v1.1.4`。
本记录后续的文档提交不改变该镜像内的源码 revision。

## 上线版本

| 项目 | 现场结果 |
|---|---|
| 公网入口 | `wss://qt.ziyuexc.top/ws` |
| Compose | `/opt/qtnetworkchat/docker-compose.yml`，项目 `qtnetworkchat` |
| 重建服务 | `qqnt-server`，容器 `qtnetworkchat-server` |
| 升级前镜像 | `ghcr.io/ziyue67/qtnetworkchat-server:v1.1.0` |
| 升级后镜像 | `ghcr.io/ziyue67/qtnetworkchat-server:sha-3eeb955b25a0` |
| 镜像 digest | `sha256:f2de39425aa02e715f7be6a890a8a07c9bc28a74d91bc73a203bb604083fc0dd` |
| OCI revision | `3eeb955b25a0a18d174102c4bd2c31195ebcea5e` |
| 健康状态 | `running healthy` |
| 启动时间 | `2026-10-04 18:26:33 +08:00`；18:30 核对时重启次数为 0 |
| PostgreSQL | QPSQL，库/角色 `qqnstser`，容器 `1Panel-postgresql-trQN` |
| Redis | 容器 `1Panel-redis-vRwX`，前缀 `qqnstser` |
| 持久卷 | `qtnetworkchat_qqnt-data` → `/data` |
| 内网 | `1panel-network`；服务端 8888 没有宿主机端口绑定 |

生产 `.env` 已将 `QTNETWORKCHAT_TAG` 固定为 `sha-3eeb955b25a0`。拉取后检查了
仓库 digest 和 OCI revision；运行容器再次核对版本与健康状态。
镜像包含编译时链接的 OpenSSL provider；本次传输/聊天烟测没有验证终端之间
选择生产 E2E 或完成密钥协商，不将 WSS 成功表述成 E2E 验证。

## 备份与回滚

私有备份目录：`/opt/qtnetworkchat/backups/update-20261004-fFJNcbte/`。
目录权限为 700；其中 Compose、`.env` 和容器配置快照可能含凭据，留在服务器，
没有写入仓库。备份包含旧镜像 ID、部署配置、PostgreSQL dump 和 `/data` 压缩包。

| 备份 | SHA-256 |
|---|---|
| `qqnstser.dump` | `af018bffb15bff0e302f639748bd96895922a3b054b2c8863acb91c76d6e8e67` |
| `appdata.tar.gz` | `6ebbbb01a500171b833ca22939d16a08fdb3d9412f199a2845cf1418588c14a1` |

数据库 dump 已实际恢复到独立临时库，确认 4 个账号、0 条聊天消息，与备份时
生产库一致；恢复验证后删除了本次临时库。应用数据压缩包已检查目录可读，
未在生产数据卷上执行覆盖恢复。数据库和数据卷备份在升级前生成，没有将二者
宣称为暂停写入后的原子快照。

旧镜像保留为 `qtnetworkchat-server:rollback-20261004`，原镜像 ID 为
`sha256:29c0ced50c6cd6effa9f5265467721a4cf9afe3c6c0035a248b61bbde3b41502`。
服务器已保存具体的回滚脚本：

```bash
sudo bash /opt/qtnetworkchat/backups/update-20261004-fFJNcbte/rollback.sh
docker inspect qtnetworkchat-server \
  --format '{{.Config.Image}} {{.State.Status}} {{.State.Health.Status}}'
```

脚本恢复此次备份的 `.env`，用本地旧镜像只重建 `qqnt-server`，并核对旧镜像 ID。
它不覆盖数据库或附件。真正需要数据恢复时，应另行确定恢复点和停写窗口。
一次部署前校验误用了 Compose 的镜像列表顺序，触发旧服务重建；校验已改为
读取 `services.qqnt-server.image` 后重新执行，最终版本是上表的新镜像。

## 公网业务与真实存储结果

验证程序从本机直接访问公网域名，未关闭证书链或主机名校验。

| 检查 | 实际结果 |
|---|---|
| TLS | `TLSv1.3`，证书 `*.ziyuexc.top`，链/主机名校验通过 |
| WebSocket | `/ws` 返回 HTTP 101，同时校验 `Sec-WebSocket-Accept` |
| 注册 | 两个随机隔离账号通过公网 WSS 收到 `login_success` |
| 真实 PostgreSQL | 查询 `qqnstser.accounts` 确认两个测试账号持久化 |
| 真实 Redis | `PING=PONG`；两个在线 presence key；业务频道有订阅者 |
| 在线私聊 | 接收账号收到本次唯一标记，发送者/接收者 ID 正确 |
| Redis 发布 | 单独订阅真实 `qqnstser:pubsub:messages`，观察到该私聊事件 |
| 离线队列 | 断开接收者后发送第二条私聊，查询到 1 条离线队列记录 |
| 再次登录 | 使用原测试密码以登录模式连接，收到 `login_success` 和离线消息 |
| 消息落库 | 两条测试消息的状态依次为 `direct` / `offline`，重放后队列为 0 |
| 清理 | 本轮测试账号/消息/会话/群成员记录清理完成，Redis 测试 presence 为 0 |

清理只针对本次生成的两个账号。生产账号最终仍为原来的 4 个。
脱敏结构化结果保存在上述私有备份目录的 `public-smoke-results.json`。
本次公网检查使用标准 WebSocket 客户端发送项目协议；Qt 的 GUI/Client 流程
证据来自已有本地与 CI 测试，不能将两种验证混称为一次桌面端完整人工验收。

## 关联服务核对

下列 6 个容器的 ID、启动时间与升级前快照完全一致；有健康检查的均为 healthy：

- `qtnetworkchat-gateway`
- `tauri-qqnt-qqnt-server-1`
- `tauri-qqnt-qqnt-ws-gateway-1`
- `1Panel-postgresql-trQN`
- `1Panel-redis-vRwX`
- `1Panel-openresty-ekrJ`

旧 `qq.ziyuexc.top/ws` 也通过严格 TLS 和 HTTP 101 验证，旧库仍为 `qq_nt`。
新入口继续使用 loopback 18081，旧入口继续使用 loopback 18080。

全计划剩余范围见 [计划当前状态与剩余验收](plan-completion-status.md)。
