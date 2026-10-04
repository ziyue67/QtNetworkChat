# 2026-10-04 生产容器升级与验证

本次已实际登录服务器并完成 QtNetworkChat 服务端升级。运行的业务源码来自
`dd0becc571c9a98522378c5af0027544205933cb`；桌面版本状态见发布记录。
本记录后续的文档提交不改变该镜像内的源码 revision。

## 上线版本

| 项目 | 现场结果 |
|---|---|
| 公网入口 | `wss://qt.ziyuexc.top/ws` |
| Compose | `/opt/qtnetworkchat/docker-compose.yml`，项目 `qtnetworkchat` |
| 重建服务 | `qqnt-server`，容器 `qtnetworkchat-server` |
| 升级前镜像 | `ghcr.io/ziyue67/qtnetworkchat-server:sha-dce239d5cda9` |
| 升级后镜像 | `ghcr.io/ziyue67/qtnetworkchat-server:sha-dd0becc571c9` |
| 镜像 digest | `sha256:84a5445a982544e319e2f135d269efd3de5216c3b3281f96f57b59bbcf4339ce` |
| OCI revision | `dd0becc571c9a98522378c5af0027544205933cb` |
| 健康状态 | `running healthy` |
| 启动时间 | `2026-10-04 22:21:17 +08:00`；公网复验后重启次数为 0 |
| PostgreSQL | QPSQL，库/角色 `qqnstser`，容器 `1Panel-postgresql-trQN` |
| Redis | 容器 `1Panel-redis-vRwX`，前缀 `qqnstser` |
| 持久卷 | `qtnetworkchat_qqnt-data` → `/data` |
| 内网 | `1panel-network`；服务端 8888 没有宿主机端口绑定 |

生产 `.env` 已将 `QTNETWORKCHAT_TAG` 固定为 `sha-dd0becc571c9`。按上列不可变
digest 拉取，再设置本地 SHA 标签并核对 OCI revision；运行容器再次核对版本
与健康状态。同一源码的 main/tag 构建具有不同版本标签，不能仅凭 SHA 标签
推断两次构建的 digest 相同。本次使用 v1.1.5 tag 构建的镜像。
镜像包含编译时链接的 OpenSSL provider；本次传输/聊天烟测没有验证终端之间
选择生产 E2E 或完成密钥协商，不将 WSS 成功表述成 E2E 验证。

## 备份与回滚

私有备份目录：`/opt/qtnetworkchat/backups/update-20261004-FLCiDOqf/`。
目录权限为 700；其中 Compose、`.env` 和容器配置快照可能含凭据，留在服务器，
没有写入仓库。备份包含旧镜像 ID、部署配置、PostgreSQL dump 和 `/data` 压缩包。

| 备份 | SHA-256 |
|---|---|
| `qqnstser.dump` | `8376311c11725d243257a920cb7439174999431f16a6f7a3162fa76c458398ea` |
| `appdata.tar.gz` | `71775d44fab96037546662bb4dcdfdef60a623dcd3ad4cdad3edaf9f68c84d62` |

数据库 dump 已实际恢复到独立临时库，确认 4 个账号、0 条聊天消息，与备份时
生产库一致；恢复验证后删除了本次临时库。应用数据压缩包已检查目录可读，
未在生产数据卷上执行覆盖恢复。数据库和数据卷备份在升级前生成，没有将二者
宣称为暂停写入后的原子快照。

旧镜像保留为 `qtnetworkchat-server:rollback-before-routefix-20261004`，原镜像 ID 为
`sha256:c64b628954dc1c26ef3bcd9664a2e36ee01899296db8a911b46cea765e972b5b`。
服务器已保存具体的回滚脚本：

```bash
sudo bash /opt/qtnetworkchat/backups/update-20261004-FLCiDOqf/rollback.sh
docker inspect qtnetworkchat-server \
  --format '{{.Config.Image}} {{.State.Status}} {{.State.Health.Status}}'
```

脚本恢复此次备份的 `.env`，用本地旧镜像只重建 `qqnt-server`，并核对旧镜像 ID。
它不覆盖数据库或附件。真正需要数据恢复时，应另行确定恢复点和停写窗口。
升级脚本按服务名核对 `services.qqnt-server.image`，只重建此服务；健康检查
失败会恢复旧配置/镜像。前两轮的独立备份目录 `update-20261004-fFJNcbte` 和
`update-20261004-8F0Vh7uO` 仍保留，本轮没有覆盖其中的恢复材料。

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
