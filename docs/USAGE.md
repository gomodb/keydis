# KeyDis 使用说明（0.3）

![KeyDis Dashboard](dashboard.png)

## 1. 启动与连接

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
./build/keydis
```

左侧 **Connections** 面板保存多套连接，支持分组（Group）；双击连接或点 **Connect** 打开。连接配置只保存非敏感字段，密码在首次连接后写入 **KWallet**。也可以直接使用连接栏临时连接。

连接栏填写：

| 字段 | 说明 |
|---|---|
| Host | Redis/Valkey 主机，默认 `127.0.0.1` |
| Port | 默认 `6379` |
| TLS | 使用 `QSslSocket`，默认校验证书与主机名 |
| ACL user | Redis 6+ 用户名；为空时使用 `default` |
| Password | 写入 KWallet；KWallet 不可用时仅本次进程使用 |
| DB | standalone 数据库编号；Cluster 固定 0 |
| Read-only | 禁用编辑、添加/删除、TTL 与 CLI 写入，仅允许浏览/查看 |
| Connect | 连接后自动执行 `HELLO 3`、可选 `SELECT` |
| Sentinel | Profile 中启用；通过 `SENTINEL get-master-addr-by-name` 解析主节点 |
| SSH | Profile 中启用；本地 `ssh -L` 隧道，仅支持密钥/agent（BatchMode） |

连接成功后，KeyDis 自动执行一次 `SCAN`（默认 `*`）。

## 2. Dashboard

连接后默认显示 **Dashboard**：

- **Server**：Redis 版本、OS、Process ID；
- **Memory**：Used / Peak / Lua memory；
- **Stats**：Connected clients、Total connections、Total commands；
- **Key Statistics**：`INFO keyspace` 中的 db、keys、expires、avg TTL；
- **Auto refresh**：默认 5 秒刷新一次；
- **Refresh**：立即重新执行 `INFO`；
- 双击 Key Statistics 的某一行，切换到对应 DB 并打开 Browser。

Dashboard 只执行一次 `INFO`，不会逐 Key 发命令；适合作为连接后的健康概览。

## 3. Key 浏览

- **DB**：浏览器工具栏的 DB 下拉框；切换后执行 `SELECT` 并重新 SCAN。
- **Tree / List**：Tree 按 `:` 分组为命名空间树，List 为平铺表；默认 Tree。
- **Tree 计数**：显示当前已加载 Key 的数量；点击 **Index all** 完整扫描当前 DB 后变为精确计数（会占用更多内存）。
- **跨连接复制**：Key 右键 → **Copy to connection...**，选择另一个 Profile，`DUMP` + `RESTORE REPLACE`。
- **双击命名空间**：把 pattern 设为 `prefix:*`，切换到 List 并重新扫描。
- **Pattern**：传给 `SCAN MATCH`，例如 `user:*`、`*session*`。空值等价于 `*`。
- **Type**：`SCAN TYPE` 过滤；服务端不支持时自动回退为客户端过滤。
- **Scan**：从 cursor 0 开始新扫描，清空当前列表。
- **Load more**：继续当前 cursor，不清空列表。
- **TTL**：显示 `persistent`、`missing` 或人类可读的剩余时间；小于 60 秒标红。
- Key 列表使用 `SCAN`，不会执行 `KEYS *`；单页最多 500 个 Key（`COUNT 500`）。
- 重复 Key 会自动去重。
- **Index all**：完整扫描当前 pattern/type，用于精确命名空间计数；大库会占用较多内存，可随时用 **Scan** 取消并重新开始。

> Tree 默认只展示已加载项；大 Key 空间请配合 pattern 使用。

## 4. 查看与编辑

选中 Key 后，右侧面板按类型显示：

| 类型 | 读取 | 编辑 |
|---|---|---|
| String | `GET` | 文本框 + `SET ... KEEPTTL` |
| Hash | `HSCAN` 分页，500/页 | 修改值 `HSET`；添加/删除字段 |
| List | `LRANGE` 分页，500/页 | 修改元素 `LSET`；头/尾插入；按值删除 |
| Set | `SSCAN` 分页，500/页 | 添加 `SADD`；删除 `SREM` |
| ZSet | `ZSCAN` 分页，500/页 | 修改分数/添加 `ZADD`；删除 `ZREM` |
| Stream | `XRANGE - + COUNT 200` | 添加 `XADD`；删除 `XDEL`；组/Pending 仍用 CLI |
| RedisJSON | `JSON.GET key path` | 路径输入 + `JSON.SET key path` raw 编辑 |
| 其他模块类型 | 只读提示 | 使用 CLI |

Hash/List/Set/ZSet 每次加载最多 500 个元素；点击 **Load more** 继续。所有修改会刷新到第一页。

### TTL

点击 **TTL...**：

- `-1`：`PERSIST`，移除过期时间；
- 其他非负秒数：`EXPIRE key seconds`。

### 删除

点击 **Delete**，确认后优先 `UNLINK`，失败时回退 `DEL`。删除成功后从列表移除。

### String 保存

点击 **Save string** 使用 `SET key value KEEPTTL`。旧版 Redis 不支持 `KEEPTTL` 时会显示错误；可改用 CLI。

### Key 右键菜单

Key 列表或命名空间树中的 Key 支持：

- **Rename...**：`RENAME`；
- **Delete...**：`UNLINK`，失败回退 `DEL`；
- **Copy key**：复制 Key 名到剪贴板；
- **Copy value**：按类型读取并格式化为文本；
- **Copy to connection...**：复制到另一个已保存的 Profile；
- **Filter this namespace**（命名空间）：按 `prefix:*` 过滤。

只读模式下重命名/删除被禁用。

## 5. Tools

**Tools** 页签提供：

- **Slowlog**：`SLOWLOG GET 128`；
- **Clients**：`CLIENT LIST`，解析地址、名称、DB、命令、age/idle；
- **Pub/Sub**：独立连接，支持 `SUBSCRIBE`/`UNSUBSCRIBE` 与 `PUBLISH`；
- **Monitor**：独立连接，Start/Stop `MONITOR`；
- **Analysis**：按 pattern/type 扫描，使用 `MEMORY USAGE`/`PTTL`，按大小排序输出 Top 500；
- **Transfer**：见下一节。

Pub/Sub 与 Monitor 不占用主浏览器连接。

## 6. Transfer（导入/导出）

**Tools → Transfer**：

- **Export...**：按 pattern 扫描当前 DB，对每个 Key 执行 `DUMP`/`PTTL`，写 JSON Lines：
  ```json
  {"key":"<base64>","dump":"<base64>","ttl":12345}
  ```
- **Import...**：逐行执行 `RESTORE key ttl dump REPLACE`；
- **Cancel**：取消后续批次；
- 导出/导入使用二进制安全的 base64，不依赖文本编码；
- **Copy to connection...** 可从 Key 右键直接复制到另一个 Profile（目标已有 Key 会被 `REPLACE`）；暂不支持 SSH/Sentinel 目标。

适合备份/迁移 Key，不包含 RDB 文件。

## 7. CLI

CLI 页签支持：

- 命令历史（当前会话）；
- 引号与反斜杠转义；
- RESP3 结构化结果显示；
- `MONITOR`/`SUBSCRIBE` 等流式命令尚未提供专用视图，请谨慎使用。

示例：

```text
PING
GET user:1
HGETALL user:1:profile
SCAN 0 MATCH session:* COUNT 100
INFO memory
```

CLI 与浏览器共用主连接；阻塞命令会占用该连接。Pub/Sub/Monitor 请使用 **Tools** 页签的独立连接。

## 8. 配置与日志

| 数据 | 路径 |
|---|---|
| 连接配置 | `~/.config/keydis/connections.json` |
| 中文翻译 | 内置 `:/i18n/keydis_zh_CN.qm`，安装时也复制到 `share/keydis/translations` |
| 密码 | KWallet（folder `keydis`） |
| 会话状态 | `~/.local/state/keydis/session.json`（预留） |
| 日志 | `~/.local/state/keydis/keydis.log` |
| 缓存 | `~/.cache/keydis/` |

连接配置只保存 ID、名称、分组、Host、Port、TLS、用户名、DB、只读标记，**不保存密码**。密码写入 KWallet；KWallet 不可用时每次连接手动输入。配置使用 `QSaveFile` 原子写，并保留 `.bak`。

日志默认 info，按 2 MB × 3 份轮转；`AUTH`、`password=` 等敏感片段会被替换为 `***`。

## 9. 当前限制

- 没有 Cluster 专用 UI；Sentinel 通过 Profile 解析主节点，但不做持续 failover 监控；
- SSH 隧道使用系统 `ssh` 的 BatchMode，仅支持密钥/agent，不支持 SSH 密码；
- Sentinel 认证复用主连接用户名/密码，暂不支持独立 Sentinel ACL；
- 只读模式是 UI 层保护，不是服务端 ACL；生产环境仍建议使用只读 ACL 用户；
- 精确命名空间计数需要 `Index all`，大库会占用较多内存；
- Stream 组/Pending 可查看，但 consumer claim/ack 工作流、RedisJSON 树形控件、模块类型专用编辑器未实现；
- CLI 的阻塞命令会占用主连接；Pub/Sub/Monitor 请用 Tools；
- 中文翻译已内置（`zh_CN`），其他语言尚未提供；
- KRunner 插件尚未提供；
- Arch 包位于 `packaging/arch/`；Flatpak 明确不在 v1.0 范围。

## 10. 测试

```bash
ctest --test-dir build --output-on-failure
```

真实 Redis：

```bash
KEYDIS_REDIS_PORT=6399 ./build/keydis_tests
KEYDIS_REDIS_PORT=6399 ./build/keydis_ui_tests
```

UI 测试使用 `QT_QPA_PLATFORM=offscreen`，验证连接、SCAN、Tree、DB 切换、String/Hash 查看与编辑、分页、只读。

## 11. 故障排查

- **连接失败**：检查 Host/Port/TLS/ACL；TLS 默认校验证书，自签名证书暂不支持忽略选项。
- **SCAN 很慢**：pattern 过宽或 Key 很多；使用更具体的前缀，或减少 `COUNT`（代码内固定 500）。
- **无法编辑**：确认没有勾选 Read-only，且 ACL 用户有写权限；Hash/List/ZSet 的字段/成员列是只读的，编辑值或分数列。
- **日志位置**：查看 `~/.local/state/keydis/keydis.log`；如果目录不可写，检查 XDG 环境变量与权限。
