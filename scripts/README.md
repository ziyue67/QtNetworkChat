# 脚本入口与维护规则

在仓库根目录运行以下命令。Windows 打包和计划任务依赖 Windows；语法与共享
函数回归可以使用 PowerShell 7 在 Linux 上执行。

| 用途 | 入口 | 实际行为 |
|---|---|---|
| 脚本验收 | `./scripts/test-script-maintenance.ps1` | 全量语法解析、路径与哈希回归、临时证据生成 |
| Windows 本地 Redis 客户端 | `./scripts/start-with-redis.ps1 -BuildDir build -Port 6380 -Prefix qtchat-local` | 检查 Redis，只在需要时启动本地实例，再启动已构建客户端 |
| Windows 安装包 | `./scripts/package-windows-installer.ps1 -BuildDir build -Configuration Release -PackageDir dist` | 制作 setup.exe，依赖 Qt 部署工具和 Inno Setup |
| Linux 安装包 | `bash scripts/package-linux-deb.sh build dist 1.1.4` | 制作 amd64 deb；版本须与当前 CMake 版本一致 |
| 桌面截图 | `./capture-screenshot.ps1 -ExePath ./build/Release/QtNetworkChat.exe -NewInstance` | Windows 交互式会话中启动与捕获窗口 |
| PostgreSQL 迁移 | `scripts/migrate-sqlite-to-postgres.ps1` | 执行迁移工具；使用前阅读 [PostgreSQL 运维](../docs/postgresql-operations.md) |
| 运行诊断 | `scripts/collect-qtnetworkchat-diagnostics.ps1` | 收集现有运行证据；参数见脚本的 param 块 |
| 大文件/S3 运维 | `scripts/run-large-file-governance.ps1`、`scripts/minio-s3-smoke.ps1` | 治理/对实际配置的对象存储执行烟测，需要相应环境 |
| S3 失败演练说明 | `scripts/s3-failure-drill.ps1` | 输出场景并分析提供的日志，不自动注入故障 |
| 证据归档 | `scripts/package-*-evidence.ps1`、`scripts/package-release-*.ps1` | 按各自参数读取证据、生成报告/包；不是 GitHub 上传命令 |

GitHub 发布由 `.github/workflows/` 管理：main 的 CI 验证代码，版本 tag 发布
Windows exe/Linux deb 和容器镜像。旧证据编排脚本 `refresh-release-closeout.ps1`
仍服务其原有本地档案格式，不是当前双安装包发布入口。

`qt-script-common.ps1` 集中路径与 SHA-256 处理；25 个调用方已改为加载它，旧
CLI 参数保持兼容。`qt-governance-common.ps1` 专门处理对象存储证据规则。添加
同类功能先复用公共函数，并在两个平台的脚本验收中验证调用方。

本次保留仍被 CMake/运维流程使用的脚本。今后调整证据格式或合并任务编排时，
须同步更新调用方和检查，不能只删除看起来重复的文件。
