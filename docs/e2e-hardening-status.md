# E2E 实现、状态层与验证边界

当前项目是 C++/Qt 客户端与服务端。默认 E2E 后端仍是草案协议；编译时链接
OpenSSL 不等于运行时已经选择生产后端。传输的 WSS/TLS 与消息 E2E 是两套
独立机制，不能用“HTTPS 正常”证明 E2E 已启用。

## 实际算法和输出

| 路径 | 选择方式 | 加密输出的 suite | 实现 |
|---|---|---|---|
| 草案 | 默认构建/默认运行时选择 | `draft-placeholder` | 31 位草案 DH、自写 streamXor/HMAC，仅用于协议开发 |
| OpenSSL | 构建开启两项生产选项，运行时设置 `QTNETWORKCHAT_E2E_CRYPTO_BACKEND=production` | `x25519-hkdf-sha256-aes-256-gcm` | X25519、HKDF-SHA256、AES-256-GCM，协议签名使用 Ed25519 |

两项构建选项是 `QTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO=ON` 和
`QTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON`。要求拒绝草案路径时，设置
`QTNETWORKCHAT_E2E_REQUIRE_PRODUCTION_CRYPTO=1`；后端不可用时操作被阻止。
回归检查直接核对实际加密结果的 suite。内部常量 `E2EProductionSuite` 只命名
生产协议，没有把草案算法包装成 AES-GCM。

## 代码阅读顺序

| 文件 | 职责 |
|---|---|
| [e2eenvelope.cpp](../src/e2eenvelope.cpp) | 公共密钥/签名/协商/加解密入口，选择实际操作路径 |
| [e2e_backend_selection.cpp](../src/e2e_backend_selection.cpp) | 后端选择、provider 注册、状态缓存、公开状态查询与 probe 入口 |
| [e2e_provider_runtime.cpp](../src/e2e_provider_runtime.cpp) | ABI 表校验、回调分发、成功/篡改/非法密钥的运行自检 |
| [e2e_openssl_provider.cpp](../src/e2e_openssl_provider.cpp) | OpenSSL 的实际密码学操作 |
| [e2e_envelope_codec.cpp](../src/e2e_envelope_codec.cpp) | 协议 JSON 编解码、格式校验 |
| [e2e_crypto_primitives.cpp](../src/e2e_crypto_primitives.cpp) | 草案算法与协议材料编码 |
| [e2e_backend_contracts.cpp](../src/e2e_backend_contracts.cpp) | 操作规格、输入输出约定、manifest 和槽位报告 |
| [e2e_backend_provider_status.cpp](../src/e2e_backend_provider_status.cpp) | provider 表、注册状态、绑定和操作 harness 报告 |
| [e2e_backend_probes.cpp](../src/e2e_backend_probes.cpp) | 显式执行 probe、往返与输出形状证据 |
| [e2e_backend_invocation.cpp](../src/e2e_backend_invocation.cpp) | preflight、调用帧、sandbox/vector/execution 状态报告 |
| [e2e_backend_review.cpp](../src/e2e_backend_review.cpp) | 候选执行、交接、可调用接口、授权与执行验收报告 |
| [e2e_backend_acceptance.cpp](../src/e2e_backend_acceptance.cpp) | 数据平面桥接、兼容性、就绪性和 rollout 证据 |

内部类型/跨模块声明位于 `src/e2e_backend_status_p.h`，没有重新增加对外的包装
API。注册指针与缓存保留单一所有者，状态查询继续返回原有 schema；显式执行
probe 与普通状态查询的区分也保留。此次是职责拆分，原有报告层的总复杂度并未
因此消失。

## 可以证明什么

默认构建运行 `E2EEnvelopeProtocol`，覆盖草案输出与 provider 未链接时的拒绝
行为；链接生产 provider 的构建运行 `E2EProductionAdapterRuntime`，覆盖真实
生产操作与负例。另有私聊/文件协议回归、运行时门控和证据包检查，详见
[测试覆盖](testing-coverage.md)。

这些检查证明受测路径按约定运行。它们不等于独立密码学审计，也不证明公开
服务器上的每个客户端都选用了生产 E2E。provider ID 中的 `reviewed` 是项目
内部名称，不能把它写成第三方安全认证。
