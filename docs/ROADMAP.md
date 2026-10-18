# KeyDis 阶段与路线图

## 1. 估算假设

- 1 名全职资深 C++/Qt 开发者；兼职 UI/UX、QA、文档支持。
- 每周 5 个开发日；不含长假和不可控的依赖阻塞。
- 开发环境：Arch Linux、Qt 6.12、KF6 6.31、CMake 4.4、Ninja。
- 发布基线：Qt 6.8+、KF6 6.10+；CI 额外验证当前 Arch 版本。
- 集成测试优先使用 CI 容器；本地可使用 Redis/Valkey 二进制或用户提供的实例。

> 25 周是“1.0 可用”的估算，不是固定承诺。每阶段结束做一次范围/进度复核。

## 2. 总览

| 阶段 | 版本 | 周期 | 主题 | 退出条件 |
|---|---|---|---|---|
| P0 | 0.1 | 3 周 | Walking Skeleton | 连接、PING/INFO、hiredis 适配、CI、语言/日志 |
| P1 | 0.3 | 4 周 | 浏览 MVP | SCAN、Key 表/树、五类只读 |
| P2 | 0.5 | 4 周 | 可编辑 Alpha | 编辑、TTL/删除、KDE 基础、Alpha |
| P3 | 0.8 | 4 周 | 生产连接 Beta | SSH/Sentinel/Cluster/ACL、只读 |
| P4 | 0.9 | 4 周 | 高级工作流 | 导入导出、复制、分析、Stream/JSON |
| P5 | 1.0-rc | 3 周 | KDE 打磨 | HIG/无障碍、文档、打包、RC |
| P6 | 1.0 | 3 周 | 硬化发布 | fuzz/soak/安全、缺陷清零、1.0 |

```text
周:  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
P0  ██████
P1        ████████
P2                ████████
P3                        ████████
P4                                ████████
P5                                        ██████
P6                                              ██████
```

## 3. 阶段详情

### P0 — Walking Skeleton（0.1，3 周，已完成）

**目标**：证明技术路线可行，建立可重复构建/测试的最小闭环。

> 已完成：Qt6/KF6 壳、NutSync 基础模块适配、hiredis v1.5.0 适配层、RESP3 连接、真实 Redis 7.4 集成测试、UI 测试、MIT/NOTICE/provenance、桌面/AppStream/安装规则。

**交付物**：

- 仓库初始化：CMake、CMakePresets、`.clang-format`、`.clang-tidy`、CI 骨架。
- ADR：UI 技术栈、连接配置/秘密存储、hiredis 解析、许可证与复用边界。
- `KXmlGuiWindow` 主窗口、KAboutData、KConfig、KDBusService 单实例。
- hiredis reader 适配层 + `RespValue` 转换 + 命令编码 + QtTest；畸形帧/超长 bulk/深嵌套测试。
- TCP/TLS transport；`HELLO 3` 失败回退 RESP2；PING/INFO。
- 直接复用并适配 NutSync 的 AppPaths/Language/Logger/Diagnostics（MIT provenance）。
- 最小连接配置模型（不含完整 UI）。
- 最小 CLI 面板：输入命令、显示结构化结果/错误/耗时。
- 集成测试脚本：CI 容器 + 本地二进制；测试数据准备/清理。
- 许可与合规：MIT `LICENSE`、`NOTICE`、`LICENSES/`、NutSync provenance、hiredis BSD-3 声明、SPDX/REUSE 检查。
- 初版线框图：主窗口、连接对话框、Key 浏览、值编辑器、CLI。

**验收**：

- 在 Arch 上从零构建成功，CI 绿。
- 连接本地 Redis/Valkey，执行 `PING`、`INFO`、`CLI` 命令。
- hiredis 适配层对 RESP3 map/set/double/verbatim/push 转换正确；RESP2 回退正确。
- 随机畸形帧不崩溃、不越界；超限回复被预算机制拒绝；ASan/UBSan 无错误。
- 语言解析、日志轮转/脱敏有单测；`reuse lint`、MIT/BSD-3 通知与 provenance 检查通过。
- 关闭窗口后 I/O 线程/连接干净退出。

**演示**：启动 KeyDis → 新建连接 → 连接 `redis://127.0.0.1:6379` → PING/INFO → CLI 查看 RESP3 结果。

---

### P1 — 浏览 MVP（0.3，4 周，核心完成）

**目标**：可以安全、流畅地浏览真实 Key 空间。

> 已完成：SCAN 分页/pattern/type 过滤、命名空间树/List、DB 选择、TTL、连接配置持久化、只读模式、Dashboard、真实 Redis 测试。树计数为已加载项，精确全库计数待后续。

**交付物**：

- 连接配置对话框：直连、TLS、Unix Socket、DB、ACL、只读、颜色/分组、测试连接、最近连接。
- KWallet 集成；秘密不落盘。
- SCAN 引擎：模式、类型、COUNT、取消、进度；Cluster 预留接口。
- Key 表格：增量加载、固定行高、多选、TTL/Type 列、分页/加载更多。
- 命名空间树：分隔符配置、路径压缩、懒加载、估算计数。
- 五类核心类型只读查看：String、Hash、List、Set、ZSet。
- String 文本/Hex 视图；大值预览 + 显式全量加载。
- Tab/Document 管理；连接状态、错误提示、重连按钮。
- 只读模式基础版：写操作禁用 + 执行器拦截。
- CLI 只读历史、基础补全。
- 设置页：语言、日志级别/保留、诊断导出（脱敏）。
- 性能基准：10 万 Key 扫描/滚动/内存。

**验收**：

- 10 万 Key 下滚动流畅，主线程无 >50 ms 阻塞。
- 不执行 `KEYS`；默认不全量加载；缓存上限生效。
- 五类类型可查看并正确分页；二进制值不丢失。
- 关闭 tab/窗口无泄漏、无悬空请求。
- 网络断开/恢复有明确状态，不崩溃。

---

### P2 — 可编辑 Alpha（0.5，4 周，完成）

**目标**：成为开发/测试环境可日常使用的客户端，发布 Alpha。

> 已完成：五类核心类型编辑、Stream/JSON、TTL/删除、只读模式、多连接/KWallet、集合分页、Pub/Sub/Monitor/Slowlog/Clients、中文翻译。

**交付物**：

- 五类编辑器写入：String、Hash、List、Set、ZSet；新增/修改/删除。
- TTL 查看/修改/持久化；重命名；复制/移动（同 DB 优先）。
- 多选批量 UNLINK（回退 DEL）；确认对话框显示数量/样例。
- 撤销/重做、脏状态、Apply/Discard、刷新冲突提示。
- Pub/Sub：频道/模式订阅、消息表、暂停/清空、发布。
- INFO 仪表盘：关键指标、内存/命中率/连接数、简单 sparkline。
- Slowlog、Clients、MONITOR（独立 lane、限流/采样）。
- KDE：KWallet、KNotification、KCommandBar、标准快捷键、系统主题、中文翻译、语言切换、会话恢复。
- 连接配置导入/导出（不含秘密）。
- CLI 历史、执行日志、基础命令补全。
- 编辑集成测试：真实 Redis/Valkey，覆盖事务/部分失败。

**验收**：

- 五类类型可安全编辑；错误路径不产生静默半写。
- 只读模式无法通过 UI/CLI 写入。
- 批量删除有预演、进度、取消。
- Alpha 包可安装/启动；崩溃可恢复会话。
- 关键路径集成测试通过；无 P1 缺陷。

---

### P3 — 生产连接 Beta（0.8，4 周，基本完成）

**目标**：覆盖生产环境常见连接方式，发布 Beta。

> 当前 0.4.2 完成 standalone + TLS + ACL + RESP2 回退 + Sentinel 解析 + SSH key/agent 隧道 + Cluster slot 路由/跨 master SCAN + Cluster 节点快照。剩余：Cluster resharding/聚合指标、持续 failover 监控、SSH 密码。

**交付物**：

- SSH 隧道：密码/私钥/agent/passphrase、known_hosts、超时/keepalive。
- Sentinel：master 发现、认证/TLS、`+switch-master` 重连。
- Cluster：拓扑发现、slot 路由、MOVED/ASK、Cluster SCAN、多 Key 分组。
- ACL 用户名/密码；只读副本选项。
- 重连/退避/拓扑刷新；节点故障隔离。
- 生产标签、危险命令确认、只读强制、审计日志。
- 连接矩阵测试：Redis 6.2/7.x/8.x、Valkey 7/8；TLS/ACL/SSH/Sentinel/Cluster。
- Cluster 节点/槽位视图；故障注入测试。

**验收**：

- 连接矩阵全部通过；SSH+Cluster 在容器环境可复现。
- 主从切换、节点宕机不崩溃；拓扑刷新后命令恢复。
- Cluster 多 Key 命令不跨 slot 误发；无法安全拆分时明确拒绝。
- 只读/生产保护不可被普通 UI 路径绕过。
- Beta 发布说明与已知限制。

---

### P4 — 高级工作流（0.9，4 周，大部分完成）

**目标**：覆盖迁移、清理、分析、流数据等高频进阶场景。

> 已完成：DUMP/RESTORE JSON Lines 导入导出、Stream 条目/组/Pending 查看与 claim/ack、RedisJSON 树形标量编辑、Key 右键/跨连接复制、内存/大 Key 分析。未完成：RedisJSON 复杂结构与数组编辑。

**交付物**：

- DUMP/RESTORE 导入导出；流式处理，避免大文件全量入内存。
- 跨连接复制 Key（同类型/同 slot 校验；Cluster 路由）。
- 内存分析：采样 `MEMORY USAGE`，按命名空间/类型汇总。
- 大 Key 分析：`STRLEN`/`LLEN`/`HLEN`/`SCARD`/`ZCARD`/`XLEN`，Top N、可取消。
- TTL 分布/过期扫描；只读分析。
- Stream 完整视图：条目、组、消费者、Pending、XACK/XCLAIM/XGROUP。
- RedisJSON raw/tree 编辑；未知模块只读兜底。
- 二进制/压缩查看器：Hex、Base64、gzip/zstd/brotli（可选）。
- 分屏；Key 详情 Inspector；值搜索。
- 自定义 Viewer 的受控原型（默认关闭，超时/参数限制）。

**验收**：

- 完成一次真实 Key 迁移（standalone + cluster 各一次）。
- 大 Key/内存分析可取消，内存有上限。
- Stream 组/Pending 操作正确，破坏性操作有确认。
- 性能回归通过；无新增 P1 缺陷。

---

### P5 — KDE 打磨与 RC（1.0-rc，3 周，部分完成）

**目标**：达到发布候选质量，完成 KDE 桌面体验与分发。

**交付物**：

- KDE HIG 审计：布局、间距、图标、快捷键、菜单、状态栏、错误提示。
- 无障碍：键盘全流程、焦点顺序、可访问名称/描述、高对比度。
- KRunner 插件：搜索连接并打开。**已完成**（`libkeydisrunner`）。
- 用户文档：快速开始、连接配置、Cluster/SSH/Sentinel、只读/安全、故障排查。
- AppStream 元数据、`.desktop`、图标、截图；`desktop-file-validate`/`appstreamcli` 通过。
- Arch PKGBUILD、Flatpak manifest；安装/卸载/升级测试。
- KCrash、日志导出（脱敏）、首次启动向导。
- 中文翻译校对；英文源字符串完整。
- 缺陷清零：无 P1；P2 有明确计划。

**验收**：

- RC 在 Arch 与 Flatpak 环境安装运行。
- 键盘可完成“连接 → 浏览 → 查看 → 编辑 → 关闭”全流程。
- 打包检查通过；文档覆盖所有 v1 功能。
- 无已知数据损坏/只读绕过问题。

---

### P6 — 1.0 硬化（3 周）

**目标**：稳定发布 1.0。

**交付物**：

- libFuzzer 持续跑 RESP/CLI 解析；修复所有崩溃/越界。
- 72 小时 soak test：反复连接/扫描/编辑/PubSub/断线重连。
- 安全审查：秘密存储、日志、TLS/SSH 校验、只读拦截、导入导出路径。
- 性能调优与最终基准；内存泄漏检查。
- 发布说明、已知限制、升级说明、校验和/签名。
- 1.0 标签与制品。

**验收**：

- 关键路径无未解决崩溃/数据损坏。
- ASan/UBSan 干净；fuzz 无新崩溃。
- 安全审查项全部关闭或有明确接受记录。
- 1.0 发布。

## 4. 关键依赖与关键路径

```text
P0 hiredis 适配/Transport
  └─ P1 SCAN/Key 浏览
       ├─ P2 编辑器/安全
       │    └─ P4 导入导出/分析
       └─ P3 Cluster/SSH/Sentinel
            └─ P4 跨连接复制/Cluster 迁移
                 └─ P5/P6 发布
```

- hiredis 适配层与连接/传输是所有后续工作的地基，P0 不通过不进入 P1。
- SCAN/缓存模型决定大 Key 空间体验，P1 必须做性能门禁。
- Cluster 路由与 SSH 是 P3 的最大不确定性，需提前准备测试环境。
- KDE 打包不是最后才做；P0 起就在 CI 中验证构建依赖。

## 5. 人力配置建议

| 角色 | 投入 | 职责 |
|---|---|---|
| 资深 C++/Qt | 1.0 FTE | 核心、UI、连接、性能 |
| C++/Qt 第二人 | 0.5–1.0 FTE | Cluster/SSH/测试/编辑器（可选） |
| UI/UX | 0.2–0.3 FTE | KDE HIG、线框图、图标/文案 |
| QA | 0.2–0.3 FTE | 连接矩阵、回归、发布验收 |
| 文档/翻译 | 0.1–0.2 FTE | 用户文档、中文校对 |

- 1 人：约 25 周。
- 2 人（core/connectivity + UI/editors）：约 15–17 周，但需要更严格的接口划分。
- 不建议在 P0–P1 之前扩到 3 人以上，沟通成本会超过收益。

## 6. 质量门禁

每个阶段合并前必须满足：

- 构建：Arch 当前环境 + 最低基线（Qt 6.8/KF6 6.10）编译通过。
- 测试：单元/集成绿；新增核心逻辑有测试。
- 静态检查：clang-format、clang-tidy 无阻断项。
- 内存：ASan/UBSan 无新增错误；关闭资源无泄漏。
- 性能：不劣化既有基准超过 10%。
- 安全：新增命令有分类；新增秘密路径不落盘；日志不泄露敏感值。
- 许可：新增第三方依赖/代码有许可审查；`reuse lint`、SPDX、`NOTICE` 完整。
- 文档：用户可见行为变更更新文档/CHANGELOG。

发布门禁：

- 无 P1 缺陷；P2 有明确修复计划。
- 连接矩阵、性能基准、soak test 通过。
- 打包/安装/卸载/升级通过。
- 许可证/第三方依赖/商标审查完成；MIT + BSD-3 通知与 NutSync provenance 完整。

## 7. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| SCAN 语义导致树计数不准/重复 | 高 | 中 | UI 标注“已扫描/估算”；有界缓存；完整索引显式触发 |
| Cluster 多 Key/事务语义复杂 | 高 | 高 | 按 slot 分组；同 slot 事务；拒绝不安全拆分；真实集群测试 |
| SSH+Cluster 内部地址不可达 | 中 | 高 | 单 session 多 channel；地址映射；文档化 NAT/容器 |
| 模块类型范围失控 | 高 | 中 | v1 只保证核心 + JSON；未知类型只读；插件后置 |
| KF6/Flatpak 版本碎片 | 中 | 中 | 基线 6.10；CI 早期构建 Flatpak；避免新 API |
| 编辑并发覆盖 | 中 | 高 | 显式 Apply、脏状态、刷新提示；后续 WATCH/CAS |
| KWallet 不可用 | 中 | 中 | 每次询问；不明文落盘；文档说明 |
| 范围膨胀/延期 | 高 | 高 | v1 功能表为合同；P4/P5 可裁剪；每阶段复核 |
| 缺少真实生产样本 | 中 | 中 | 合成 100 万 Key 数据；社区 Beta 反馈；脱敏数据集 |
| hiredis 不限制单 bulk 大小 | 中 | 中 | 连接层字节预算/分块 feed；超限断开；fuzz 覆盖 |
| hiredis ABI/行为变化 | 低 | 中 | 固定最低版本；封装在 `core/resp`；升级跑 fixture/fuzz |
| NutSync 复用文件缺少 provenance | 中 | 高 | 文件头保留 SPDX/来源/commit；`NOTICE` 记录重新许可；代码审查 + `reuse` |
| qredisclient 维护停滞 | 高 | 中 | 不采用；仅作 API/历史参考 |

## 8. 成功指标

### 8.1 产品指标

- 10 万 Key 浏览不冻结；100 万 Key 内存有上限。
- 连接成功率高；错误信息可操作。
- 只读模式零写入；批量操作可取消。
- 用户可在 3 分钟内完成“安装 → 连接 → 查看一个 Key”。

### 8.2 工程指标

- `RespValue`/hiredis 适配层单测覆盖率 ≥ 90%；核心层 ≥ 80%。
- 主线程单次阻塞 < 50 ms。
- 1.0 前 72 小时 soak 无崩溃/泄漏增长。
- 发布包在 Arch + Flatpak 可安装运行。
- 无 P1 安全/数据损坏缺陷。

## 9. 最近三周（P0）任务清单

| ID | 任务 | 产出 |
|---|---|---|
| K-001 | 初始化 CMake/CMakePresets/CI | 可构建空壳 |
| K-002 | KXmlGui 主窗口 + KAboutData + KDBusService | 可启动窗口 |
| K-003 | 复用/适配 NutSync LanguageManager | 语言解析 + Qt translator + provenance |
| K-004 | 复用/适配 NutSync Logger/Diagnostics | 诊断与日志规范 + provenance |
| K-005 | hiredis reader 适配 + `RespValue` 转换 | 解析 + 单测 |
| K-006 | 命令编码 + CLI 词法 | 命令编码 + 单测 |
| K-007 | TCP/TLS transport + 连接状态机 | PING/INFO |
| K-008 | HELLO 3 / AUTH / SELECT 握手 | 兼容 Redis 6/7/8、Valkey |
| K-009 | 最小 CLI 面板 | 命令执行/结果树 |
| K-010 | 集成测试脚本 + 测试数据 | CI 可重复 |
| K-011 | MIT LICENSE/NOTICE/REUSE/provenance | 合规基线 |
| K-012 | 主窗口/连接/浏览线框图 | 设计基线 |

**P0 退出演示**：见 [PLAN.md](PLAN.md) 第 8 节。

## 10. 版本发布节奏

| 版本 | 内容 | 发布条件 |
|---|---|---|
| 0.1 | Walking Skeleton | P0 验收 |
| 0.3 | 浏览 MVP | P1 验收 |
| 0.5 Alpha | 可编辑 | P2 验收 |
| 0.8 Beta | 生产连接 | P3 验收 |
| 0.9 | 高级工作流 | P4 验收 |
| 1.0-rc | KDE 打磨 | P5 验收 |
| 1.0 | 稳定版 | P6 验收 |
