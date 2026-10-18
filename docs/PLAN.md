# KeyDis 总体方案

## 1. 产品定义

**一句话**：面向 KDE 的原生 Redis/Valkey 工作台，兼顾大 Key 空间浏览、日常编辑、生产环境安全与桌面集成。

**目标用户**：

- 使用 Linux/KDE 的后端、SRE、DevOps 和全栈开发者；
- 需要同时管理本地、测试、预发、生产多套 Redis/Valkey 的团队；
- 对 Electron 客户端的内存占用、启动速度和桌面一致性不满意，但又不想退回命令行的人。

**核心场景**：

1. 快速连接并浏览键空间，定位某个业务前缀下的 Key。
2. 查看/编辑 String、Hash、List、Set、ZSet、Stream、JSON。
3. 在 CLI 中执行临时命令，检查 INFO、Slowlog、Clients、Pub/Sub。
4. 在 Cluster/Sentinel/SSH/TLS 环境中安全操作。
5. 迁移或清理一批 Key，并在执行前知道影响范围。
6. 在生产环境只读查看，避免误操作。

## 2. 差异化

参考 [RDM（现 RESP.app）](https://github.com/redis/RedisDesktopManager) 与 [Another Redis Desktop Manager](https://github.com/qishibo/AnotherRedisDesktopManager) 的功能边界，但不复制其代码、资源、文案或具体交互。KeyDis 的差异点：

| 方向 | 具体做法 |
|---|---|
| 原生 KDE | Qt Widgets + KF6，KXmlGui/KWallet/KIO/KRunner，低内存、系统主题、快捷键 |
| 安全优先 | 只读模式、生产标签、危险命令确认、批量预演、审计日志 |
| 大 Key 空间 | SCAN 增量 + 有界缓存 + 命名空间聚合，默认不加载全部 Key |
| 运维洞察 | TTL 分布、内存/大 Key 分析、集群槽位与节点视图 |
| 可取消 | 所有长任务进入 TaskCenter，有进度、上限、取消 |
| 二进制安全 | 全链路 `QByteArray`，提供 UTF-8/Hex/Base64/转义/压缩视图 |

## 3. v1.0 范围

### 3.1 必须交付

| 领域 | 功能 |
|---|---|
| 连接 | 直连、TLS、Unix Socket、SSH、Sentinel、Cluster、ACL、只读、分组/颜色、导入导出、URI/CLI 参数 |
| 浏览 | SCAN、树/平铺、模式/类型/TTL/大小过滤、分页、多选、批量 UNLINK、重命名、TTL、复制/移动 |
| 类型 | String、Hash、List、Set、ZSet、Stream、RedisJSON；未知类型安全只读 |
| 工作台 | CLI、Monitor、Pub/Sub、INFO 仪表盘、Slowlog、Clients、Config/ACL 查看、执行日志 |
| 数据工作流 | DUMP/RESTORE 导入导出、跨连接复制、大 Key/内存/TTL 分析 |
| KDE | KXmlGui、KConfig、KWallet、KIO、KSyntaxHighlighting、KNotifications、KStatusNotifierItem、KRunner、系统主题、i18n、无障碍 |
| 平台 | Linux/KDE（Wayland/X11） |

### 3.2 延后或不做的内容

- 完整 RDB 文件解析、定时备份、快照管理；
- RediSearch 查询构建器、Vector/TimeSeries 图形化编辑器；
- 云同步、团队共享、遥测、账号体系；
- 跨 slot 的伪事务、违反 Cluster 语义的自动改写；
- 默认执行任意本地脚本/格式化程序；
- 默认 `KEYS *`、默认全量加载；
- Windows/macOS 首发支持（1.x 再评估）。

### 3.3 功能优先级

| 优先级 | 内容 |
|---|---|
| P0 | 连接、SCAN 浏览、核心类型只读、CLI、hiredis/RESP2/3、错误处理 |
| P1 | 核心类型编辑、TTL/删除/重命名、只读保护、KWallet、会话恢复 |
| P2 | SSH、Sentinel、Cluster、ACL、Pub/Sub、INFO、Slowlog、Monitor |
| P3 | 导入导出、跨连接复制、内存/大 Key/TTL 分析、Stream 组、RedisJSON |
| P4 | KRunner、KIO 远程文件、自定义查看器、分屏、更多模块类型 |

## 4. 架构摘要

```text
UI (Qt Widgets / KF6)
  └─ Models & Controllers
       └─ Services (Session / Keyspace / Value / Cluster / Safety)
            └─ Command Router / SCAN Engine / Topology
                 └─ HiredisReader (RESP2/3) + Executor + Lanes
                      └─ TCP / TLS / Unix / SSH
```

关键设计：

- **协议层**与 UI 完全解耦，`keydis_core` 可独立测试；解析使用 hiredis reader API，命令编码与类型化 `RespValue` 由 KeyDis 负责。
- 每个 endpoint 一个 I/O 工作线程，主线程只做模型更新和绘制。
- 主命令、阻塞命令、Pub/Sub、Monitor 使用独立 lane，避免互相卡死。
- Key 浏览使用 SCAN + 有界 LRU 缓存 + 命名空间聚合器，而不是把全部 Key 塞进 `QStandardItemModel`。
- Cluster 使用独立 `ClusterRouter`，处理 slot 映射、MOVED/ASK、拓扑刷新和多 Key 分组。
- 编辑使用 `QUndoStack`，显式 Apply；同 slot 多步修改使用事务，否则明确报告部分成功。
- 秘密只进 KWallet；日志默认脱敏。

详细设计见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 5. 阶段与版本

| 阶段 | 版本 | 周期 | 里程碑 |
|---|---|---|---|
| P0 可行性 | 0.1 | 3 周 | Walking Skeleton：窗口、hiredis 适配、TCP/TLS、PING/INFO、CLI、语言/日志骨架 |
| P1 浏览 MVP | 0.3 | 4 周 | 连接配置、SCAN、Key 表/树、五类只读查看 |
| P2 可编辑 Alpha | 0.5 | 4 周 | 五类编辑、TTL/删除、Pub/Sub、INFO、KDE 基础集成 |
| P3 生产连接 Beta | 0.8 | 4 周 | SSH、Sentinel、Cluster、ACL、只读/审计 |
| P4 高级工作流 | 0.9 | 4 周 | 导入导出、跨连接复制、内存/TTL 分析、Stream/JSON |
| P5 KDE 打磨 | 1.0-rc | 3 周 | HIG/无障碍、KRunner、文档、Flatpak/Arch、RC |
| P6 1.0 硬化 | 1.0 | 3 周 | fuzz/soak/安全审查、缺陷清零、发布 |

总周期按 1 名全职资深开发者估算约 **25 周**；2 名开发者可压缩到 **15–17 周**。详细交付物、验收标准和风险见 [ROADMAP.md](ROADMAP.md)。

## 6. 关键决策

| ID | 决策 | 状态 |
|---|---|---|
| D1 | 主界面使用 Qt Widgets，不使用 Kirigami/QML | 建议，待确认 |
| D2 | 基线 Qt 6.8+ / KF6 6.10+，开发环境 Qt 6.12 / KF6 6.31 | 建议 |
| D3 | 连接配置用版本化 JSON，秘密用 KWallet，不落明文 | 建议 |
| D4 | Key 浏览 SCAN-first，默认不全量加载 | 设计原则 |
| D5 | 许可证 MIT | 已确认 |
| D6 | 应用 ID `io.github.keydis.KeyDis` | 待确认 |
| D7 | v1.0 必须包含 Cluster、SSH、Sentinel | 待确认 |
| D8 | 协议解析使用 hiredis reader API | 已确认 |
| D9 | 不使用 qredisclient | 已确认 |
| D10 | 直接复用 NutSync 的 AppPaths/Language/Logger/Diagnostics，版权人重新许可为 MIT | 已确认 |


## 7. 许可证与代码复用

### 7.1 许可证：MIT

- KeyDis 应用代码使用 **MIT**；
- 源文件使用 `SPDX-License-Identifier: MIT`；
- 发布包包含 `LICENSE`、`NOTICE` 与第三方许可文本；
- hiredis 为 BSD-3-Clause，与 MIT 兼容。

### 7.2 依赖许可矩阵

| 组件 | 许可 | 结论 |
|---|---|---|
| hiredis v1.5.0 | BSD-3-Clause | 使用 reader API；保留版权/许可/免责声明 |
| qredisclient | LGPL-2.1 | 不使用：归档、Qt5、静态库 |
| KDE Frameworks 6 | LGPL-2.1-or-later | 动态链接；随包提供许可 |
| Qt 6 | LGPL-3.0 / GPL-3.0 / 商业 | 采用 LGPL 动态链接；提供通知与源码获取方式 |
| NutSync 基础代码 | MIT（版权人重新许可） | 直接复用 AppPaths/Language/Logger/Diagnostics |
| NutSync 原仓库 | GPL-3.0-or-later | 不作为 KeyDis 的许可依据 |
| NutSync AUR 打包 | 0BSD | 可参考/复用 |

### 7.3 NutSync 直接复用范围

直接复用并适配：

- `app_paths`：XDG config/state/cache 路径；
- `language`：system/en/zh_CN 偏好、解析、Qt translator；
- `logger`：级别、轮转、stderr + 文件双写；
- `diagnostics`：脱敏报告、日志尾部（按 KeyDis 裁剪）。

不直接复用 WebDAV/同步业务代码。复用文件保留：

- SPDX MIT 标识；
- `Copyright (c) 2026 sunhowcn`；
- 来源仓库、commit `f7fd60730110f942911e56eefa8c6771abb3eb2a`；
- “originally GPL-3.0-or-later; relicensed to MIT by the copyright holder”说明。

详见 [ADR-0004](adr/0004-license-and-reuse.md)。

## 8. 当前进度与下一步

当前版本为 **0.3 工作台**：

- CMake/Ninja + Qt 6 + KF6 + hiredis v1.5.0；支持系统 hiredis 发行版构建；
- Dashboard、命名空间树/List、DB 切换、SCAN 分页、Index all 精确计数；
- String/Hash/List/Set/ZSet/Stream/RedisJSON 查看与编辑；
- Key 右键菜单：重命名、删除、复制 Key/Value、跨连接复制；
- 多连接配置/分组、KWallet、只读模式、RESP2 回退；
- Sentinel 主节点解析、SSH key/agent 本地隧道；
- Tools：Slowlog、Clients、Pub/Sub、Monitor、内存/大 Key 分析；
- Transfer：DUMP/RESTORE JSON Lines 导入导出，可取消；
- 中文翻译 catalog、Arch PKGBUILD；
- 真实 Redis 7.4 集成测试（含 Sentinel/PubSub/Transfer/Memory）、UI 测试、ASan 验证。

尚未实现（留给后续）：

1. Cluster 连接与 slot 路由；
2. Sentinel 持续 failover 监控、独立 Sentinel ACL；
3. SSH 密码认证、主机密钥管理 UI；
4. KRunner 插件、跨连接批量迁移 UI；
5. Stream consumer claim/ack 完整工作流、RedisJSON 树形控件；
6. Flatpak（明确排除）、更多发行版包。

## 9. 已确认与待确认

已确认：

- UI：Qt Widgets + KF6，不做 Kirigami 主界面；
- 许可证：MIT；
- 协议解析：hiredis v1.5.0 reader API，不使用 qredisclient；
- 直接复用 NutSync 的 AppPaths/Language/Logger/Diagnostics（MIT 重新许可）。

仍需确认：

- 应用 ID：`io.github.keydis.KeyDis`（发布前替换）；
- v1.0 包含 Cluster、SSH、Sentinel；
- 首批目标：Arch Linux + Flatpak，Windows/macOS 延后。
