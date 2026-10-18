# ADR-0002：连接配置使用版本化 JSON，秘密使用 KWallet

- 状态：Proposed
- 日期：2026-10-18
- 决策者：KeyDis 项目
- 相关：KConfig、KWallet、连接导入导出、安全

## 背景

KeyDis 需要保存：

- 连接分组、名称、颜色、最近使用时间；
- 直连/TLS/Unix/SSH/Sentinel/Cluster 参数；
- ACL 用户名、DB、只读标记、生产标签；
- 密码、SSH 私钥口令等秘密；
- 窗口/会话/快捷键等应用状态。

需求：

1. 秘密不落明文；
2. 配置可版本化、可迁移；
3. 支持导入/导出与备份；
4. 写入原子，避免半写损坏；
5. 不因 KWallet 不可用而阻塞应用启动；
6. 跨桌面环境仍可运行（但优先 KDE）。

## 决策

采用**分层存储**：

| 数据 | 位置 | 格式 |
|---|---|---|
| 应用设置/窗口/快捷键 | KConfig | `~/.config/keydisrc` |
| 连接配置 | `~/.config/keydis/connections.json` | 版本化 JSON |
| 秘密 | KWallet | 按 profile UUID 索引 |
| 会话 | `~/.local/state/keydis/session.json` | JSON |
| 审计 | `~/.local/state/keydis/audit.jsonl` | JSON Lines，可关闭 |
| 缓存/缩略图 | `~/.cache/keydis/` | 可清理 |

### 连接配置示例

```json
{
  "schemaVersion": 1,
  "profiles": [
    {
      "id": "5b0d7f5e-7a4b-4f0e-9d5f-2f0a6f1c8e11",
      "name": "local-dev",
      "group": "Development",
      "color": "#3daee9",
      "environment": "dev",
      "readOnly": false,
      "endpoint": {
        "kind": "tcp",
        "host": "127.0.0.1",
        "port": 6379,
        "db": 0
      },
      "tls": {
        "enabled": false,
        "verifyPeer": true,
        "caFile": "",
        "clientCertFile": "",
        "clientKeyFile": ""
      },
      "ssh": null,
      "sentinel": null,
      "cluster": {
        "enabled": false,
        "useReplicas": false
      },
      "auth": {
        "username": "",
        "secretRef": "connection/5b0d7f5e-7a4b-4f0e-9d5f-2f0a6f1c8e11/password"
      },
      "behavior": {
        "keySeparator": ":",
        "scanCount": 500,
        "connectTimeoutMs": 5000,
        "commandTimeoutMs": 15000,
        "keyCacheLimit": 200000
      },
      "createdAt": "2026-10-18T22:00:00+08:00",
      "lastUsedAt": "2026-10-18T22:00:00+08:00"
    }
  ]
}
```

### 秘密引用

- KWallet folder：`keydis`
- entry 命名：
  - `connection/<uuid>/password`
  - `connection/<uuid>/ssh-password`
  - `connection/<uuid>/ssh-key-passphrase`
  - `sentinel/<uuid>/password`
- JSON 只存 `secretRef` 或布尔标记，不存秘密本身。
- KWallet 打开失败/用户拒绝：本次会话每次询问；不写入明文文件。
- 导入/导出默认剥离秘密；可选择在同一 KWallet 内重新绑定，但不导出明文。

### 写入与迁移

- `QSaveFile` 原子写；写入前备份 `connections.json.bak`。
- 顶层 `schemaVersion` 驱动迁移函数；迁移失败保留原文件并提示。
- 导入时逐项校验，重复 UUID 生成新 ID 或提示覆盖。
- 配置文件权限 `0600`；目录 `0700`（在 `$HOME` 下尽量）。

## 理由

1. **JSON 适合嵌套结构**：SSH/TLS/Sentinel/Cluster 配置层级深，KConfig group 表达笨重。
2. **版本化/迁移清晰**：`schemaVersion` + 纯函数迁移，易测试。
3. **导入导出方便**：JSON 可直接审阅、diff、备份；秘密单独处理。
4. **KWallet 是 KDE 原生**：与 KDE 体验一致，避免自造加密格式。
5. **KConfig 仍适合应用状态**：窗口、快捷键、最近文件等与 KDE 集成更自然。
6. **失败可降级**：KWallet 不可用不影响只读/临时连接。

## 后果

### 正面

- 秘密与配置解耦，便于安全审计。
- 配置可版本化、可测试迁移。
- 导入导出不会意外泄露密码。
- 不引入数据库依赖。

### 负面

- 需要维护 JSON schema 与迁移逻辑。
- KWallet 在非 KDE 桌面上可能不可用；需要清晰的降级提示。
- 多实例并发写需要文件锁或单实例约束（KDBusService 已限制单实例）。
- 用户手动编辑 JSON 可能破坏配置；需要校验与错误提示。

## 备选方案

| 方案 | 结论 |
|---|---|
| 全部用 KConfig | 嵌套配置笨重，导入导出/迁移差；只用于应用状态 |
| SQLite | 查询/迁移强，但增加依赖与并发/备份复杂度；v1 不需要 |
| 明文 JSON 存密码 | 违反安全原则；排除 |
| 自研加密文件 | 密钥管理复杂，容易出错；优先 KWallet |
| libsecret | 跨桌面好，但 KDE 集成/依赖不如 KWallet 直接；可作为未来后端接口 |
| 每条连接一个 KConfig 文件 | 文件多、导入导出麻烦；不采用 |

## 安全考虑

- 日志、崩溃报告、审计中不得出现秘密。
- 导出配置时默认移除 `secretRef` 或标记为不可用。
- KWallet 条目在删除连接时一并删除；提供“清理孤儿秘密”工具。
- 未来可抽象 `SecretStore` 接口，支持 KWallet/libsecret/无存储三种后端。
- TLS 私钥路径只存路径，不复制私钥内容。
