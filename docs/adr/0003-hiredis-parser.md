# ADR-0003：协议解析采用 hiredis reader API

- 状态：Accepted
- 日期：2026-10-18
- 决策者：KeyDis 项目
- 相关：RESP2/RESP3、Qt 网络、SSH、许可证

## 背景

KeyDis 需要解析 Redis/Valkey 的 RESP2/RESP3 回复，并满足：

- 二进制安全（键值可包含 NUL/任意字节）；
- 增量解析（TCP 分片、粘包）；
- RESP3 类型（map、set、double、bignum、verbatim、push、attribute）；
- 与 Qt 的 TCP/TLS/Unix socket、libssh 通道、独立 lane 共存；
- 对恶意/异常服务端的资源限制；
- 宽松许可证，便于长期开源分发。

候选：

1. 自研 RESP codec；
2. [hiredis](https://github.com/redis/hiredis) 完整客户端/异步 API；
3. hiredis 的 reply parsing API（`redisReader`）；
4. [qredisclient](https://github.com/uglide/qredisclient)；
5. 其他 C/C++ Redis 库。

## 决策

**使用 hiredis 的 reply parsing API，仅做解析；不使用它的完整客户端/异步 API，也不使用 qredisclient。**

具体边界：

- 使用：
  - `redisReaderCreate()`
  - `redisReaderFeed()`
  - `redisReaderGetReply()`
  - `redisReaderFree()`
  - `freeReplyObject()`
- 不使用：
  - `redisContext` / `redisAsyncContext`
  - hiredis 的 libevent/libuv/libev 适配器
  - hiredis 的 SSL 封装（TLS 由 `QSslSocket` 负责）
  - qredisclient 的连接/传输/响应抽象
- KeyDis 保留：
  - `RespValue` 类型与转换器；
  - 命令编码（RESP Array of Bulk Strings）；
  - 错误分类、超时、lane、重连、拓扑；
  - 字节预算与安全限制。

数据流：

```text
QTcpSocket / QSslSocket / QLocalSocket / libssh channel
  → ConnectionWorker
  → redisReader (hiredis)
  → redisReply* → RespValue（复制、检查、释放）
  → CommandExecutor → Service → UI
```

## 理由

### 为什么用 hiredis reader

- **成熟、专注**：reader 是独立于 I/O 的流式解析器，正好匹配 Qt 的 socket 模型。
- **RESP3 支持**：1.2+ 已提供 map/set/double/bool/bignum/verbatim/push/attribute 类型。
- **二进制安全**：`redisReply` 带 `str/len`，可直接复制为 `QByteArray`。
- **BSD-3-Clause**：宽松许可，适合 Apache-2.0/MIT 应用；保留版权与免责声明即可。
- **可测试**：reader 可脱离网络，用 fixture/随机字节直接测；便于 fuzz。

### 为什么不用 hiredis 完整客户端

- 它的 I/O/SSL/event loop 与 KeyDis 的 Qt socket、TLS 配置、SSH 通道、lane 模型冲突。
- 我们需要的超时、取消、重连、拓扑、命令分类都在更高层；使用完整客户端会形成两套连接状态。
- reader API 已经把最容易出错的协议解析部分解决，命令编码与 I/O 自己控制更简单。

### 为什么不用 qredisclient

- GitHub 仓库已 **archived**，最后推送约 2022 年；README 明确“Supported Qt versions: 5.6–5.9”。
- 许可证为 **LGPL-2.1**，不是 MIT/BSD 级宽松许可；其 qmake 工程默认构建静态库，静态链接的 LGPL 合规成本更高。
- 自带连接/传输/响应层，移植到 Qt6 + RESP3 + Cluster/SSH 的工作量大于直接使用 hiredis reader。
- 可以作为 API/历史参考，但不作为依赖。

## 集成细节

### 版本与构建

- 锁定 **hiredis v1.5.0**（2026-10-15 上游最新稳定版）。`maxdepth` 字段从 1.5 起可用；1.4.1 仅作为兼容后备。
- 优先系统包：`pkg-config hiredis` 或 CMake config；Flatpak 在 manifest 中构建/打包。
- CI 固定测试版本；升级时跑全部 RESP fixture、fuzz、真实 Redis/Valkey 集成。

### 限制

hiredis reader 自身提供：

- `maxdepth`：v1.5.0 设为 **128**；KeyDis 转换器同时强制 128 层上限；
- `maxelements`：默认 2^32-1，KeyDis 设为 **1,000,000**（可配置降低）；
- `maxbuf`：仅控制空闲缓冲区，不是单条回复上限。

hiredis **不限制单个 bulk 长度**，因此 KeyDis 必须在连接层增加：

- 单条回复字节预算，默认 **64 MiB**；
- 固定大小 `feed()` chunk（如 64 KiB）；
- 超过预算立即断开并报告，不继续缓冲；
- 大 String 使用 `STRLEN` + `GETRANGE` 分块预览，而不是默认 `GET` 全量。

### 转换

- `REDIS_REPLY_MAP` / `ATTR`：hiredis 以数组承载，转换为 `pairs`；
- `REDIS_REPLY_PUSH`：路由到 Pub/Sub/Monitor，不进入普通命令回复队列；
- `REDIS_REPLY_DOUBLE`：保留 `dval` 与原始字符串；
- `REDIS_REPLY_VERB`：保留 `vtype`；
- `REDIS_REPLY_BIGNUM`：保留字符串，不转 `qint64`；
- 转换后立即 `freeReplyObject()`，异常路径也要释放。

### 测试

- 固定 RESP2/RESP3 fixture（含嵌套 map、push、attribute、nil、空 bulk、二进制）。
- 随机分片：同一字节流按 1/2/3/7/64KiB 分片，结果一致。
- 畸形帧：负长度、超长长度、深嵌套、未闭合数组、非法 verbatim。
- 预算：声明超大 bulk 的流必须被拒绝，内存不超上限。
- fuzz：对 reader 输入随机字节；ASan/UBSan。

## 后果

### 正面

- 减少自研 parser 的协议边界风险。
- RESP3 类型覆盖更快。
- BSD-3 许可简单，适合宽松开源。
- 解析与 I/O 解耦，Qt/SSH/lane 架构保持不变。

### 负面

- 增加一个 C 依赖与构建/打包工作。
- hiredis 不限制单 bulk；需要 KeyDis 自己补预算。
- C API 需要 RAII 包装，避免泄漏。
- 上游行为/ABI 变化需要升级测试。

## 备选方案

| 方案 | 结论 |
|---|---|
| 自研 RESP codec | 可控但边界多、测试成本高；不作为首选 |
| hiredis 完整客户端 | 与 Qt/SSH/lane 架构冲突；不采用 |
| hiredis reader | **采用** |
| qredisclient | 归档、Qt5、LGPL、静态库；不采用 |
| 其他库 | 需重新评估许可/RESP3/Qt6/维护状态；暂不引入 |

## 后续

1. P0 完成 `core/resp/HiredisReader` RAII 封装与转换器。
2. 添加 `LICENSES/BSD-3-Clause.txt`、`NOTICE`、`third_party/hiredis/README.md`。
3. 加入 fixture/fuzz/预算测试。
4. Flatpak manifest 增加 hiredis 模块；Arch PKGBUILD 增加 `hiredis` 依赖。
5. 1.0 前复核 hiredis 版本与 CVE。

## 参考

- [hiredis](https://github.com/redis/hiredis)（BSD-3-Clause）
- [hiredis README：Reply parsing API / RESP3 / limits](https://github.com/redis/hiredis#reply-parsing-api)
- [qredisclient](https://github.com/uglide/qredisclient)（LGPL-2.1，已归档）
