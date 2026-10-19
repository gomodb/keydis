# KeyDis

> KDE 原生的 Redis / Valkey 桌面客户端

[![CI](https://github.com/gomodb/keydis/actions/workflows/ci.yml/badge.svg)](https://github.com/gomodb/keydis/actions/workflows/ci.yml)

KeyDis 使用 **C++20、Qt 6 Widgets 和 KDE Frameworks 6** 构建，面向 Linux/KDE，提供低内存、响应快、数据密集型的 Redis/Valkey 工作台。

![KeyDis Dashboard](docs/dashboard.png)

![KeyDis Browser](docs/tree-browser.png)

## 功能

- **连接**：Standalone、TLS（CA/客户端证书）、Unix Socket、SSH、SSH + Cluster（SOCKS5）、Sentinel、Redis Cluster、ACL、KWallet、只读模式。
- **多连接**：连接标签页、左右分屏、可拖动分隔条、KDE 风格菜单/工具栏。
- **Key 浏览**：SCAN 分页、命名空间树、类型/pattern 过滤、TTL、多选批量删除/TTL、跨连接复制。
- **值查看与编辑**：String、Hash、List、Set、ZSet、Stream、RedisJSON、GEO。
  - String：二进制安全编辑、Hex/Base64、gzip/zlib/zstd/Brotli、MessagePack、Protobuf、Bitmap、HyperLogLog、大值分段加载。
  - Hash：字段级 TTL（Redis 7.4+）。
  - 集合：Apply/Discard、Undo/Redo、全量搜索。
  - Stream：条目、组、Pending、消费者、claim/ack、DELCONSUMER、XAUTOCLAIM。
- **工具**：Dashboard、Slowlog、Clients、Pub/Sub、Monitor、内存分析、Cluster 节点、DUMP/RESTORE 导入导出。
- **KDE 集成**：KRunner、系统主题、KWallet、中文界面。

当前版本：**0.4.39**。

## 构建

依赖：

- Qt 6.8+
- KDE Frameworks 6
- CMake 3.28+、Ninja、C++20 编译器
- hiredis
- zlib、zstd、brotli、msgpack-c

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build build --parallel
```

发行版打包建议使用系统 hiredis：

```bash
cmake -S . -B build -G Ninja \
  -DKEYDIS_USE_SYSTEM_HIREDIS=ON \
  -DBUILD_TESTING=OFF
```

## 运行

```bash
./build/keydis --host 127.0.0.1 --port 6379 --connect
```

也支持 `--tls`、`--username`、`--password`、`--db`、`--unix-socket`、`--cluster` 和 `--tab dashboard|browser|cli|tools`。

## 测试

```bash
ctest --test-dir build --output-on-failure

# 真实 Redis 集成测试
redis-server --daemonize yes
KEYDIS_REDIS_PORT=6379 ./build/keydis_tests
KEYDIS_REDIS_PORT=6379 ./build/keydis_ui_tests
```

CI 使用 Arch Linux 容器构建 x86_64/ARM64，运行 GCC、Clang、ASan/UBSan 与真实 Redis 测试；tag 上生成双架构 Arch 包和 GitHub Release。

## Arch Linux

```bash
cd packaging/arch
makepkg -si
```

依赖和打包说明见 [packaging/arch/README.md](packaging/arch/README.md)。

## 文档

- [使用说明](docs/USAGE.md)
- [架构设计](docs/ARCHITECTURE.md)
- [阶段与路线图](docs/ROADMAP.md)
- [Arch 打包](packaging/arch/README.md)
- [翻译维护](translations/README.md)

## 许可

KeyDis 使用 **MIT** 许可证。第三方组件与来源说明见 [LICENSE](LICENSE)、[NOTICE](NOTICE) 和 [LICENSES](LICENSES/)。
