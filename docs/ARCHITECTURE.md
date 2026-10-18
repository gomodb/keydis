# KeyDis 架构设计

## 1. 目标与约束

### 1.1 目标

- 在 Linux/KDE 上提供原生、低内存、响应快的 Redis/Valkey 客户端。
- 大 Key 空间下保持 UI 可用，不因全量加载导致卡死或 OOM。
- 协议、连接、拓扑、UI 分层，核心层可独立测试和复用。
- 支持生产环境所需的 TLS、SSH、Sentinel、Cluster、ACL、只读保护。
- 与 KDE 桌面深度集成，而不是“套一层 Breeze 皮肤”。

### 1.2 约束

- C++20，Qt 6.8+，KDE Frameworks 6.10+；开发环境为 Qt 6.12 / KF6 6.31。
- 主界面 Qt Widgets；不引入 Electron/WebView。
- 键和值全程 `QByteArray`，禁止隐式、不可逆的编码转换。
- 网络 I/O 不得阻塞 GUI 主线程。
- 默认不执行 `KEYS`，默认不全量加载。
- 秘密不写入明文配置文件。

## 2. 技术栈

| 层 | 选型 |
|---|---|
| 语言 | C++20 |
| UI | Qt 6 Widgets、KXmlGui、KConfigWidgets |
| 桌面集成 | KF6：KConfig、KWallet、KIO、KI18n、KNotifications、KStatusNotifierItem、KRunner、KDBusService、KCrash |
| 协议解析 | hiredis v1.5.0 reader API（BSD-3-Clause） |
| 日志 | QLoggingCategory + 轮转文件 + 脱敏 |
| 本地化 | KI18n + QTranslator（system/en/zh_CN） |
| 诊断 | 脱敏诊断报告 + 日志尾部 |
| 文本 | QPlainTextEdit + KSyntaxHighlighting |
| 网络 | QTcpSocket、QSslSocket、QLocalSocket |
| SSH | libssh（direct-tcpip channel，复用 session） |
| 压缩 | zlib、zstd、brotli（可选） |
| 构建 | CMake + Ninja + ECM |
| 测试 | QtTest、CTest、libFuzzer、ASan/UBSan |
| 打包 | Arch PKGBUILD、Flatpak（org.kde.Platform） |

## 3. 组件图

```text
┌─────────────────────────────── UI 层 ───────────────────────────────┐
│ KeyDisWindow (KXmlGuiWindow)                                        │
│  ├─ ConnectionDock        ├─ DocumentArea (tabs/splits)             │
│  ├─ KeyBrowserDock        │   ├─ KeyBrowserView                     │
│  ├─ BottomDock            │   ├─ ValueEditorHost                    │
│  │   ├─ CliView           │   └─ ServerInfoView                     │
│  │   ├─ MonitorView       ├─ TaskCenterDock                         │
│  │   └─ PubSubView        └─ StatusBar / Notifications              │
└──────────────────────────────────────────────────────────────────────┘
                 │ signals / models / commands
┌──────────────────────────── 应用服务层 ─────────────────────────────┐
│ ConnectionManager │ SessionManager │ KeyspaceService │ ValueService  │
│ ClusterService    │ SentinelService│ InsightsService │ SafetyService │
│ TaskCenter        │ DocumentManager│ CommandHistory  │ AuditLog      │
└──────────────────────────────────────────────────────────────────────┘
                 │
┌──────────────────────────── 核心层 ─────────────────────────────────┐
│ HiredisReader │ CommandExecutor │ CommandRouter │ ScanEngine          │
│ Transport     │ Topology       │ KeyCache      │ NamespaceIndex     │
│ SecretStore   │ ProfileStore   │ CommandCatalog│ ResultFormatter    │
│ Language      │ Logger         │ Diagnostics   │ AppPaths            │
└──────────────────────────────────────────────────────────────────────┘
                 │
┌──────────────────────────── 传输层 ─────────────────────────────────┐
│ TcpTransport │ TlsTransport │ UnixTransport │ SshTransport (libssh) │
└──────────────────────────────────────────────────────────────────────┘
```

## 4. 模块职责

| 模块 | 职责 | 不负责 |
|---|---|---|
| `core/resp` | hiredis reader 封装、`RespValue` 转换、命令编码、限制与错误 | 网络、重连、TLS/SSH |
| `core/net` | Transport、连接状态、命令 FIFO、pipeline、超时、lane | 业务语义 |
| `core/conn` | ConnectionProfile、SecretStore、Session、握手 | UI 展示 |
| `core/topology` | Cluster slot 映射、Sentinel 发现、MOVED/ASK | 值编辑 |
| `core/keyspace` | SCAN 状态机、批量元数据、缓存、命名空间索引 | 表格绘制 |
| `core/value` | 按类型的读取/写入控制器、分页、校验 | 具体 Widget |
| `core/safety` | 命令分类、只读拦截、危险操作策略、审计 | 网络执行 |
| `core/insights` | 内存/大 Key/TTL 采样与分析 | 全量精确统计 |
| `ui/models` | QAbstractItemModel、过滤、排序、角色 | 网络请求 |
| `ui/editors` | 各类型编辑器、委托、撤销集成 | 协议解析 |
| `ui/dashboard` | Server/Memory/Stats、Key Statistics | Key 浏览 |
| `ui/tools` | Slowlog、Clients、Pub/Sub、Monitor、Transfer、Analysis | 编辑器 |
| `krunner` | KRunner runner，打开保存的连接 | ProfileStore |
| `ui/dialogs` | 连接配置/Profile 编辑 | 连接状态 |
| `core/platform` | 语言、日志、诊断、XDG 路径 | 业务语义 |
| `core/security` | KWallet 密码存储 | 连接状态 |
| `core/transfer` | DUMP/RESTORE JSON Lines 导入导出 | SCAN/编辑器 |
| `core/analysis` | MEMORY USAGE/PTTL 扫描与 Top Key | Key 浏览 |
| `app` | main、KAboutData、单实例、崩溃处理 | 业务逻辑 |

## 5. 连接生命周期

```text
Disconnected
   │ connect(profile)
   ▼
Resolving ──► TcpConnecting ──► TlsHandshaking ──► Authenticating
   │              │                  │                  │
   │              └─────────────── error ──────────────┘
   ▼
SelectingDb ──► Ready ◄──► Reconnecting
                   │
                   ├─ topology refresh (cluster/sentinel)
                   ├─ lane creation (blocking/pubsub/monitor)
                   └─ command execution
```

握手步骤：

1. DNS/Unix/SSH channel 建立；
2. TCP + TLS（可选）；
3. `HELLO 3`，带 ACL 用户名/密码；失败则 `AUTH` 后以 RESP2 继续；
4. `SELECT db`（仅 standalone；Cluster 固定 db 0）；
5. `CLIENT SETNAME keydis/<host>/<pid>`；
6. 可选 `CLIENT SETINFO`（服务端支持时）；
7. Cluster 执行拓扑发现；Sentinel 解析 master；
8. 状态置为 Ready，创建所需 lane。

错误分类：

- 认证失败、ACL 拒绝、TLS 证书错误：不可自动重试，提示用户修正配置。
- 连接拒绝、超时、网络中断：指数退避重连，保留只读快照。
- MOVED/ASK：由 ClusterRouter 处理，不暴露给 UI。
- READONLY/LOADING/MASTERDOWN：显示状态，按策略重试或只读降级。

## 6. 协议解析：hiredis reader + RespValue 适配

### 6.1 选型

使用 **hiredis 的 reply parsing API**，而不是它的完整客户端/异步 API：

- 只使用 `redisReaderCreate()`、`redisReaderFeed()`、`redisReaderGetReply()`、`redisReaderFree()` 和 `freeReplyObject()`。
- 不使用 `redisContext` / `redisAsyncContext` / hiredis 的 libevent/libuv 适配器；I/O 仍由 Qt（TCP/TLS/Unix）与 libssh 负责。
- 不使用 qredisclient：项目已归档，README 声明仅支持 Qt 5.6–5.9，许可证为 LGPL-2.1，且为 qmake 静态库；把它移植到 Qt6 + RESP3 + Cluster/SSH 的成本高于直接用 hiredis reader。
- KeyDis 自己保留 `RespValue` 类型、命令编码、错误分类、lane 与重连逻辑，避免把 hiredis 的 C 结构泄漏到 UI/业务层。

### 6.2 数据流

```text
QTcpSocket / QSslSocket / QLocalSocket / libssh channel
        │  bytes
        ▼
ConnectionWorker (每 endpoint 一个 I/O 线程)
        │  feed(chunk)
        ▼
HiredisReader (redisReader*)
        │  redisReply*
        ▼
RespValue 转换器（复制为 QByteArray，检查限制，释放 redisReply）
        │  typed reply / push / error
        ▼
CommandExecutor → CommandHandle → Service → Model/UI
```

### 6.3 RespValue

```cpp
enum class RespType {
    SimpleString, Error, Integer, BulkString, Array,
    Null, Boolean, Double, BigNumber, BulkError,
    Verbatim, Map, Set, Attribute, Push
};

struct RespValue {
    RespType type;
    QByteArray scalar;       // simple/error/bulk/verbatim/bignum
    qint64 integer = 0;
    double real = 0;
    QList<RespValue> elements;
    QList<QPair<RespValue, RespValue>> pairs; // map/attribute
};
```

hiredis 的 `REDIS_REPLY_MAP` / `ATTR` 在底层仍以数组返回，转换器按类型还原为 `pairs`；`SET`、`PUSH`、`VERB`、`BIGNUM`、`BOOL`、`DOUBLE` 分别映射。转换后立即 `freeReplyObject()`，不让 C 指针逃逸。

### 6.4 解析规则与限制

- 增量解析：TCP 分片、粘包都通过 `feed()` 处理；每轮 `redisReaderGetReply()` 直到返回空，支持一次读取多个 Push/回复。
- 限制：
  - `redisReader::maxdepth = 128`（v1.5.0 可用）；转换器同时强制 **128 层**上限；
  - `redisReader::maxelements = 1,000,000`（可按配置降低）；
  - 连接层维护“单条回复字节预算”，默认 **64 MiB**；超过即关闭连接并报告，不无限缓冲；
  - hiredis 本身不限制单个 bulk 长度，因此预算必须在 `ConnectionWorker`/读取层实现，不能只依赖 reader；
  - 单次 `feed()` 使用固定大小 chunk（如 64 KiB），避免一次读入不可控内存。
- 非法帧/超限：返回明确错误并关闭连接，不尝试猜测或恢复同一连接。
- Push/Attribute 单独路由，不混入普通命令回复；Pub/Sub、Monitor 使用独立 lane。
- 所有字符串按 `(char*, len)` 复制为 `QByteArray`，保留 NUL 与任意二进制内容。

### 6.5 命令编码

- KeyDis 自己实现 `CommandEncoder`：RESP Array of Bulk Strings，二进制安全。
- 不使用 hiredis 的 `redisFormatCommandArgv()`/`sds`，避免额外依赖与格式化歧义。
- CLI 输入使用 shell-like 词法解析（引号、转义），但**不执行 shell**。
- `AUTH`、`HELLO AUTH` 等敏感参数在日志/诊断中替换为 `***`。

### 6.6 依赖与许可

- 锁定版本：hiredis **v1.5.0**（2026-10-15 上游最新稳定版）。RESP3 类型与可配置 `maxdepth` 均可用；通过 CMake FetchContent 固定 tag。发行版若只提供 1.4.1，可在兼容模式下使用，但不作为默认。
- 构建：默认通过 CMake FetchContent 固定 `v1.5.0`；发行版/Flatpak 可改为系统包并在 manifest 中构建；CI 固定同版本。
- 许可：BSD-3-Clause。随包提供 `LICENSES/BSD-3-Clause.txt`、`NOTICE` 与 `third_party/hiredis/README.md`；不修改上游版权头。
- 升级策略：跟随上游稳定 tag，升级时跑 RESP fixture、fuzz、真实 Redis/Valkey 集成测试。
- 本文档不复制 hiredis 源码；只在 `core/resp` 中通过公开 API 使用。

## 7. 命令执行与 lane

### 7.1 执行器

每个 endpoint 维护：

- 待发送 FIFO；
- 在途命令队列（Redis 按序回复）；
- 连接级错误与超时；
- pipeline 批次。

```cpp
class CommandExecutor {
public:
    CommandHandle execute(Command cmd, CommandOptions opts);
    CommandHandle executeBatch(QList<Command> cmds, BatchOptions opts);
    void close();
};
```

`CommandHandle` 提供完成/失败回调、耗时、原始字节数与结构化 Reply。

### 7.2 四类 lane

| Lane | 用途 | 特点 |
|---|---|---|
| main | 普通命令、SCAN、值读写 | 可 pipeline，有超时 |
| blocking | BLPOP/BRPOP/BLMOVE 等 | 独立连接，无普通命令超时 |
| pubsub | SUBSCRIBE/PSUBSCRIBE/SSUBSCRIBE | 长连接，持续 Push |
| monitor | MONITOR | 长连接，高吞吐，需限流/采样 |

阻塞命令、Pub/Sub、Monitor 不能和普通命令共用连接，否则会阻塞后续请求。lane 按需创建，关闭 tab 时释放。

### 7.3 取消与超时

- 普通命令超时：标记连接可疑，关闭并重连；不把超时命令的回复错配给后续命令。
- SCAN/分析：批次边界检查取消标志，不强行中断在途 socket。
- 大值加载：分块 `GETRANGE`/`LRANGE`/`HSCAN`，每块检查取消。
- UI 关闭：取消所有挂起请求，等待 I/O 线程退出，避免悬空回调。

## 8. 线程模型

- GUI 主线程：模型更新、绘制、用户输入。
- 每个 endpoint 一个 `QThread` + event loop，拥有 socket 和解析器。
- 解析完成的 `RespValue` 通过 queued signal 投递主线程；UI 侧批量合并，避免逐条刷新。
- 拓扑发现、SCAN 合并在服务层线程/任务中执行，不阻塞主线程。
- 线程数量上限：Cluster 大规模节点时使用固定 I/O 线程池或限制并发连接数，避免每节点一线程。
- 所有跨线程对象使用 Qt 父子关系/智能指针明确生命周期；关闭时先停任务，再断连接。

> 实现阶段可以先做每 endpoint 一线程的简单模型，在 P3 压测后视情况改为 I/O 线程池。

## 9. Key 浏览与内存模型

### 9.1 扫描

```text
SCAN cursor MATCH pattern COUNT 500 TYPE type
  └─ 批量 TYPE / PTTL（必要时 MEMORY USAGE）
       └─ 写入 KeyCache + NamespaceIndex
            └─ 批量插入模型（每批 100–200 行）
```

- 绝不使用 `KEYS`。
- 服务端不支持 `SCAN TYPE` 时，客户端过滤并明确提示。
- Cluster：每个 master 独立 cursor；合并结果；可选去重；显示节点列。
- 副本模式：默认扫 master；只读副本模式可扫 replica，但接受延迟。
- 扫描进度显示“已扫描 N 条”，不伪造总数；`DBSIZE` 仅作参考。

### 9.2 缓存

- `KeyCache` 使用 LRU，默认上限 20 万条；可在设置中调整。
- 缓存条目包含 key、type、pttl、可选 size、node、scan generation。
- 模式/类型/连接/DB 变化时递增 generation；旧请求结果丢弃。
- 缓存不是数据库快照；刷新按钮触发新 generation。

### 9.3 命名空间树

- 按用户配置的分隔符（默认 `:`）构建前缀树。
- 路径压缩，避免为每个字符建节点。
- 节点计数为“已扫描计数”，UI 标注“估算/已扫描”。
- 完整索引是显式操作：设置内存上限、显示进度、可取消。
- 树节点懒加载子节点；不要用 `QStandardItemModel` 承载百万节点。

### 9.4 表格

- `QAbstractTableModel` + `fetchMore`；固定行高；无每行 Widget。
- 列：Key、Type、TTL、Size、Node；Size 默认按需加载。
- 排序仅对已缓存集合生效，UI 明确提示“仅排序已加载项”。
- 多选删除使用 pipeline `UNLINK`（回退 `DEL`），先展示数量与样例。
- 大 Key 详情在选中后 debounce 加载，避免快速移动时命令风暴。

## 10. 值编辑器

### 10.1 接口

```cpp
class ValueEditor : public QWidget {
public:
    virtual void load(const KeyRef& key) = 0;
    virtual bool isDirty() const = 0;
    virtual bool validate(QString* error) const = 0;
    virtual void apply() = 0;
    virtual void discard() = 0;
};
```

### 10.2 分页策略

| 类型 | 读 | 写 |
|---|---|---|
| String | `STRLEN` + `GETRANGE` 预览；全量需确认 | `SET` / `SET KEEPTTL` |
| Hash | `HSCAN`；字段搜索 | `HSET`/`HDEL`；字段 TTL 视服务端版本 |
| List | `LRANGE` 分页 + `LLEN` | `LPUSH`/`RPUSH`/`LSET`/`LREM` |
| Set | `SSCAN` | `SADD`/`SREM` |
| ZSet | `ZSCAN` 或 `ZRANGE` 按 rank/score | `ZADD`/`ZREM` |
| Stream | `XRANGE`/`XREVRANGE` + `XINFO` | `XADD`/`XDEL`/`XTRIM`；组/Pending 操作 |
| JSON | `JSON.GET` raw/tree | `JSON.SET`；路径编辑后续 |

### 10.3 编辑一致性

- 本地 `QUndoStack`；脏状态在 tab 标题显示。
- Apply 前可选择“刷新并检查变化”。
- 同 slot 多步修改使用 `MULTI/EXEC`；Cluster 跨 slot 时逐条执行并报告部分成功。
- 不做后台自动保存；不做静默覆盖。
- 大集合操作显示影响数量，并允许取消后续批次。

## 11. Cluster / Sentinel / SSH

### 11.1 Cluster

- 发现顺序：`CLUSTER SHARDS` → `CLUSTER SLOTS` → `CLUSTER NODES`。
- 维护 `slot -> node`、`node -> endpoint`、master/replica 角色。
- 命令路由：按 key 计算 CRC16 slot；无 key 命令发给任意可用节点。
- `MOVED`：刷新拓扑后重试一次；`ASK`：临时重试到目标节点，不立即刷新全量拓扑。
- 多 Key：按 slot 分组；`MGET`/`MSET`/`DEL`/`UNLINK`/`EXISTS` 等可合并；无法安全拆分时拒绝并解释。
- Cluster SCAN：每 master cursor；`DBSIZE` 仅参考；DB 选择器禁用。
- 拓扑刷新异步、去抖；节点失败时标记不可用，不阻塞其他节点。
- SSH+Cluster：通过同一 SSH session 的多个 channel 访问内部地址；必要时支持 seed 地址映射。

### 11.2 Sentinel

- 从 sentinel 列表查询 `SENTINEL get-master-addr-by-name`。
- 支持 sentinel 自身认证/TLS。
- 订阅 `+switch-master`，触发 master 地址更新与重连。
- 显示当前 master/slave 与 sentinel 状态；不在客户端做故障转移。

### 11.3 SSH

- libssh；支持密码、私钥、agent、passphrase。
- 校验 known_hosts；首次连接需用户确认指纹。
- 单 session 多 direct-tcpip channel；避免每节点重复握手。
- 连接超时、keepalive、断开重连可配置。
- 私钥口令不落盘；必要时进 KWallet。

## 12. 配置与秘密

- 应用设置：KConfig（窗口状态、快捷键、主题、最近连接）。
- 连接配置：`~/.config/keydis/connections.json`，版本化 schema，`QSaveFile` 原子写入。
- 秘密：KWallet，按 profile UUID 存储；配置文件只保存 `secretRef`。
- 导入/导出：默认不含秘密；用户可选择是否导出（仍不导出明文密码）。
- 会话恢复：`~/.local/state/keydis/session.json`；崩溃后可恢复 tab/分屏，不恢复未提交编辑。
- 缓存：`~/.cache/keydis/`；可清理。
- 审计：`~/.local/state/keydis/audit.jsonl`，只记录命令摘要、目标、结果、时间；可关闭/清空。

详细 schema 与替代方案见 [ADR-0002](adr/0002-connection-storage.md)。

## 13. 安全模型

### 13.1 只读保护

- 命令目录为每个命令标注 `read/write/admin/blocking/pubsub`。
- 只读连接在 UI 层禁用写操作，并在执行器层再次拦截（防止绕过）。
- 未知命令默认拒绝；提供“以管理员模式临时执行”的显式流程。
- 只读状态在标题栏/状态栏持续可见，不能只靠图标。

### 13.2 危险操作

- `FLUSHDB`、`FLUSHALL`、`CONFIG SET`、`DEBUG`、`SHUTDOWN`、`SCRIPT`、`EVAL`、批量删除等：
  - 生产标签连接要求输入确认短语；
  - 显示目标、影响范围、将执行的命令；
  - 审计记录。
- 批量删除先扫描/预演，显示数量和样例；执行中按批报告成功/失败。

### 13.3 秘密与日志

- 不记录 AUTH 参数、密码、私钥、完整值。
- TLS 默认校验证书与主机名；关闭需显式确认。
- SSH 默认校验 host key；不自动接受未知指纹。
- KWallet 不可用时每次询问；不退回明文存储。

## 14. UI 架构

### 14.1 主窗口

- `KXmlGuiWindow`；菜单/工具栏由 `.rc` 文件定义，可配置。
- 左侧 `ConnectionDock`：分组、最近、搜索、状态、颜色标签。
- 中央 `DocumentArea`：tab + splitter；每个 tab 是一个 Session 工作区。
- 工作区：KeyBrowser（左）+ ValueEditorHost（右），可切换上下/左右布局。
- 底部 `BottomDock`：CLI、Monitor、Pub/Sub、任务、日志，可折叠。
- 右侧可选 `InspectorDock`：Key 元数据、TTL、编码、内存。
- 状态栏：连接状态、DB、只读、扫描进度、延迟。

### 14.2 模型

- `ConnectionTreeModel`：分组/连接/状态。
- `KeyTableModel`：增量、虚拟化、多选。
- `NamespaceTreeModel`：懒加载、路径压缩、计数。
- `ValueTableModel`：按类型的列定义/委托。
- `TaskListModel`：扫描/分析/导入导出进度。
- 过滤/排序尽量在模型层做，避免 `QSortFilterProxyModel` 对大列表递归。

### 14.3 交互

- 命令面板（`KCommandBar`）：连接、刷新、过滤、切换视图、执行常用动作。
- 快捷键遵循 KDE 标准；可配置；冲突检测。
- 键盘导航：连接树、Key 表、值表、tab 切换；无障碍名称/描述。
- 错误用 `KMessageWidget` 内联展示，不用阻塞式弹窗堆叠。
- 长任务进入 TaskCenter，可后台继续；完成后 `KNotification`。

### 14.4 语言、日志与诊断

直接复用并适配 NutSync 的以下基础代码（版权人重新许可为 MIT，保留 provenance）：

- **LanguageManager**：
  - 偏好 `system` / `en` / `zh_CN`；`system` 用 `QLocale::system().language()` 解析；
  - 安装应用 translator 与 Qt translator；KI18n 设置 `keydis` domain；
  - 语言切换提示重启（或后续做动态 retranslate）；解析逻辑可单测。
- **Logger**：
  - `qInstallMessageHandler` + `QLoggingCategory`（`keydis.resp`、`keydis.net`、`keydis.cluster`、`keydis.ui`、`keydis.security`）；
  - 级别 debug/info/warning/error；stderr + 文件双写；按大小/份数轮转；
  - 默认不记录完整键值/命令参数；AUTH、密码、私钥、敏感值统一 redaction；
  - 写日志加锁，避免 GUI 线程与 I/O 线程交错破坏行。
- **Diagnostics**：
  - 生成脱敏报告：版本、Qt/KF6、OS、locale、配置/日志路径（`~` 替换）、秘密状态 `<set>/<empty>`；
  - 读取日志尾部供用户附加到 issue；不包含键值、密码、完整 URL 凭据。
- **AppPaths**：
  - 配置 `~/.config/keydis/`，状态 `~/.local/state/keydis/`，缓存 `~/.cache/keydis/`；
  - 使用 `QStandardPaths`，避免硬编码 `$HOME`。

## 15. KDE 集成

| 能力 | 实现 |
|---|---|
| 菜单/工具栏 | KXmlGui、KActionCollection、standardAction |
| 设置 | KConfigDialog、KConfigGroup |
| 秘密 | KWallet |
| 文件 | KIO/KUrlRequester；导入导出支持远程 URL |
| 语法高亮 | KSyntaxHighlighting；自定义 Redis CLI 语法 |
| 通知 | KNotification（长任务、Sentinel 切换、连接失败） |
| 托盘 | KStatusNotifierItem（可选，主要用于长任务/监控） |
| 快速启动 | KRunner 插件：搜索连接并打开 |
| 单实例 | KDBusService |
| 崩溃 | KCrash + 可选 core dump 提示 |
| 主题 | 系统 palette、Breeze 图标、暗色模式、HiDPI |
| 本地化 | KI18n；英文源字符串，中文翻译随包 |
| 关于 | KAboutData、AppStream、.desktop |

## 16. 性能预算

| 指标 | 目标 |
|---|---|
| 冷启动 | < 1.5 s（SSD，热缓存 < 800 ms） |
| 连接（本地） | p95 < 300 ms |
| 10 万 Key 扫描 | UI 不冻结；滚动 60 fps |
| 主线程阻塞 | 单次 < 50 ms |
| 100 万 Key 浏览 | 常驻内存有上限，不随全量 Key 线性增长 |
| 10 万元素集合 | 分页加载 < 1 s/页（本地） |
| 关闭 tab | 1 s 内取消任务并释放连接 |
| 空闲内存 | 典型单连接 < 250 MB |

这些指标在 P1/P3/P6 设性能门禁；不达标不进入下一阶段。

## 17. 测试策略

- **单元**：hiredis 适配/RespValue 转换、命令编码、CLI 词法、slot 计算、命令分类、SCAN 状态机、配置迁移、语言解析、日志脱敏。
- **集成**：真实 Redis/Valkey；CI 容器；本地二进制；TLS/ACL/Unix/Cluster/Sentinel/SSH 矩阵。
- **模糊**：通过 hiredis reader 输入畸形帧、超长 bulk、深嵌套、随机分片；同时验证 KeyDis 的预算限制会拒绝超限回复。
- **性能**：启动、连接、扫描、滚动、值分页、内存峰值。
- **UI**：QTest 键盘/对话框/只读拦截；可选截图回归。
- **安全**：秘密不落盘、日志脱敏、TLS/host key 校验、只读绕过、第三方许可证/NOTICE 完整性。
- **故障注入**：节点宕机、主从切换、网络延迟、错误密码、过期证书、MOVED/ASK。

## 18. 目录结构

```text
keydis/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ src/
│  ├─ app/
│  ├─ krunner/
│  ├─ core/{resp,net,conn,topology,keyspace,value,safety,insights,platform,security,transfer}/
│  ├─ ui/{mainwindow,dashboard,docks,models,editors,dialogs,tools,values}/
│  └─ krunner/
├─ third_party/            # 第三方许可/provenance；hiredis 由 FetchContent 获取，不 vendored
├─ LICENSES/
├─ NOTICE
├─ tests/{unit,integration,fuzz,bench}/
├─ packaging/{arch,flatpak,appstream}/
├─ po/
├─ docs/{adr,architecture,security,user}/
└─ scripts/
```

## 19. 扩展点

- **Viewer 插件**：v1 先内部接口；后续可支持受控的外部格式化器（参数替换、超时、无 shell）。
- **命令目录**：静态元数据 + 运行时 `COMMAND`/`COMMAND DOCS` 补全；不复制第三方文案。
- **导入导出格式**：v1 使用 DUMP/RESTORE 与 JSON Lines；后续可加 RDB 解析。
- **主题**：优先系统 palette；不硬编码颜色，图表用 `KColorScheme`。
- **脚本**：v1 不内置脚本引擎；CLI 可执行 EVAL，但受危险命令策略约束。

## 20. 第三方依赖与许可证

| 组件 | 版本/许可 | 使用方式 |
|---|---|---|
| hiredis | v1.5.0 / BSD-3-Clause | 仅 reader API；保留版权头与许可 |
| Qt 6 | LGPL-3.0 / GPL-3.0 / 商业 | 动态链接；随包提供 LGPL 通知与源码获取方式 |
| KDE Frameworks 6 | LGPL-2.1-or-later | 动态链接；随包提供许可 |
| libssh | LGPL-2.1 | 动态链接；随包提供许可；发布前按实际版本核对 |
| zlib / zstd / brotli | zlib / BSD-3 / MIT | 可选动态链接；保留通知 |
| qredisclient | LGPL-2.1 | 不使用 |
| NutSync 基础代码 | MIT（版权人重新许可） | 直接复用 AppPaths/Language/Logger/Diagnostics，保留 provenance |
| NutSync AUR 打包 | 0BSD | 可参考/复用 |

- KeyDis 应用代码采用 **MIT**；与 hiredis BSD-3 兼容，与 KF6/Qt 的 LGPL 动态链接兼容。静态链接 LGPL 库需额外的可重链接义务，发布包默认避免静态 Qt/KF6。
- NutSync 的语言/日志/诊断代码直接复用并适配；文件头保留 SPDX MIT、原作者与来源 commit，详见 [ADR-0004](adr/0004-license-and-reuse.md)。
- `LICENSES/`、`NOTICE`、SPDX 头、`reuse lint` 纳入 CI 门禁。
- 具体复用边界见 [ADR-0004](adr/0004-license-and-reuse.md)。

## 21. 未决问题

1. 应用 ID 最终确认（许可证已定为 MIT）。
2. 是否在 1.0 前支持 SOCKS5/HTTP CONNECT 代理。
3. Cluster 多 Key 命令的拆分范围（仅常用命令，还是 `COMMAND GETKEYS` 通用拆分）。
4. 自定义 Viewer 是否进入 1.0，还是 1.1。
5. Flatpak 运行时版本、hiredis 打包方式与 KF6 最低版本的对齐。
6. 是否提供 KRunner 之外的 Plasma widget（倾向于不做）。
