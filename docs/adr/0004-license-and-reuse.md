# ADR-0004：MIT 许可证与 NutSync 代码直接复用

- 状态：Accepted
- 日期：2026-10-18
- 决策者：KeyDis 项目
- 相关：MIT、BSD-3-Clause、LGPL、GPL-3.0、NutSync

## 背景

KeyDis 需要：

- 完全开源，并采用宽松许可证；
- 直接复用 `~/Projects/nutsync` 中已经验证过的语言、日志、诊断、路径等基础代码；
- 使用 hiredis 作为协议解析库；
- 保持 Qt 6 / KDE Frameworks 6 动态链接的可分发性。

约束：

- hiredis 是 **BSD-3-Clause**；
- Qt 6 是 **LGPL-3.0 / GPL-3.0 / 商业许可**；
- KDE Frameworks 6 主要是 **LGPL-2.1-or-later**；
- libssh 是 **LGPL-2.1**；
- NutSync 应用源码是 **GPL-3.0-or-later**，但其 AUR 打包文件是 **0BSD**。

## 决策

### 1. KeyDis 应用代码使用 MIT

- `LICENSE` 为 MIT；
- 源文件使用 `SPDX-License-Identifier: MIT`；
- 发布包包含 `LICENSE`、`NOTICE`、第三方许可文本。

MIT 与 hiredis BSD-3、Qt/KF6 的 LGPL 动态链接兼容。

### 2. 直接复用 NutSync 基础代码

由 NutSync 版权人授权，KeyDis **直接复用并适配**以下代码：

- `src/core/app_paths.{h,cpp}`：XDG 路径；
- `src/core/language.{h,cpp}`：语言偏好/解析/translator；
- `src/core/logger.{h,cpp}`：日志级别/轮转/文件输出；
- `src/core/diagnostics.{h,cpp}`：脱敏诊断报告/日志尾部（按 KeyDis 需要裁剪）；
- 相关 QtTest/CI 思路。

不直接复用与 KeyDis 无关的 WebDAV/同步业务代码。

### 3. 复用授权与 provenance

当前本地检查（2026-10-18）：

- NutSync `git log` 的 91 个提交均来自 `sunhowcn`；
- `src/`、`tests/`、`resources/` 未发现第三方版权头或 SPDX 标识；
- NutSync 应用源码为 GPL-3.0-or-later，AUR 打包文件为 0BSD。

在上述事实成立且无未跟踪第三方素材的前提下，版权人将相关代码以 **MIT** 重新许可给 KeyDis。KeyDis 必须记录：

- 来源仓库：`https://github.com/gomodb/nutsync`
- 来源 commit：`f7fd60730110f942911e56eefa8c6771abb3eb2a`
- 原作者/版权人：`sunhowcn`
- 原许可证：`GPL-3.0-or-later`
- 重新许可：MIT，2026-10-18
- 修改说明：命名空间/路径/日志策略/诊断字段按 KeyDis 调整

这些信息写入 `NOTICE`，并在复用文件头保留 SPDX 与来源说明。NutSync 仓库本身仍可保持 GPL-3.0-or-later 发布，不受影响。

> 如果未来发现复用文件包含第三方代码或外部贡献，必须暂停使用并重新审计。本文不是法律意见。

### 4. hiredis

- 使用 hiredis **v1.5.0**（2026-10-15 上游最新稳定版）的 reader API；
- 不采用 qredisclient（归档、Qt 5.6–5.9、LGPL-2.1、静态库）；
- BSD-3-Clause 文本放入 `LICENSES/BSD-3-Clause.txt`；
- 只使用公开 API，不修改/复制 hiredis 源码。

### 5. qredisclient

不使用。原因：

- 仓库已归档，最后推送约 2022 年；
- README 声明仅支持 Qt 5.6–5.9；
- LGPL-2.1，qmake 静态库；
- 自带连接/响应层，与 KeyDis 的 Qt6/SSH/lane 架构重叠；
- 移植成本高于直接使用 hiredis reader。

## 许可矩阵

| 组件 | 许可证 | 使用方式 |
|---|---|---|
| KeyDis 应用代码 | MIT | 本项目 |
| NutSync 复用部分 | MIT（版权人重新许可） | 保留 provenance 与版权声明 |
| NutSync 原仓库 | GPL-3.0-or-later | 不作为 KeyDis 的许可依据 |
| NutSync AUR 打包 | 0BSD | 可参考/复用 |
| hiredis | BSD-3-Clause | reader API；保留通知 |
| Qt 6 | LGPL-3.0 / GPL-3.0 / 商业 | 动态链接；LGPL 通知/源码获取方式 |
| KDE Frameworks 6 | LGPL-2.1-or-later | 动态链接；逐模块核对 |
| libssh | LGPL-2.1 | 动态链接 |
| zlib / zstd / brotli | zlib / BSD-3 / MIT | 可选；保留通知 |
| qredisclient | LGPL-2.1 | 不使用 |

## 实施要求

### 仓库

```text
LICENSE                       # MIT
NOTICE                        # KeyDis + NutSync provenance + hiredis
LICENSES/
  MIT.txt
  BSD-3-Clause.txt
third_party/
  hiredis/README.md           # v1.5.0、来源、许可、用法
```

- 复用文件头：
  ```cpp
  // SPDX-License-Identifier: MIT
  // SPDX-FileCopyrightText: 2026 sunhowcn
  // Derived from NutSync (https://github.com/gomodb/nutsync),
  // commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
  // originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.
  ```
- 新建文件头：
  ```cpp
  // SPDX-License-Identifier: MIT
  // SPDX-FileCopyrightText: 2026 KeyDis contributors
  ```

### CI

- 检查 `LICENSE`/`NOTICE`/`LICENSES` 存在；
- `reuse lint` 或等效检查；
- 检查 hiredis v1.5.0 固定版本；
- 检查安装包包含 MIT/BSD-3 通知；
- 人工 review：复用文件有 provenance 注释，未混入 NutSync 业务代码。

### 发布包

- Arch PKGBUILD：`license=('MIT')`；`depends` 包含 hiredis；安装 `LICENSE`/`NOTICE`。
- Flatpak：AppStream `project_license` 为 `MIT`；包含 Qt/KF6/hiredis 通知。

## 后果

### 正面

- 语言/日志/路径等基础设施可直接复用，减少 P0 工作量；
- MIT 满足宽松开源目标；
- hiredis BSD-3 与 MIT 兼容；
- KDE/Qt 动态链接可分发。

### 负面

- 需要维护 provenance 与第三方通知；
- 复用代码需要适配命名空间/路径/日志策略；
- 若 NutSync 未来引入第三方代码，不能自动同步；
- MIT 不提供专利授权，需在 `NOTICE`/贡献政策中说明。

## 备选方案

| 方案 | 结论 |
|---|---|
| MIT + 直接复用（本决策） | 采用 |
| Apache-2.0 + 独立重写 | 已否决（用户选择 MIT 与直接复用） |
| GPL-3.0-or-later + 直接复用 | 许可简单，但不符合宽松目标 |
| 双许可 | 暂不考虑 |

## 后续

1. P0 添加 `LICENSE`、`NOTICE`、`LICENSES/` 与 SPDX 头。
2. 复制/适配 NutSync `app_paths`、`language`、`logger`、`diagnostics`。
3. 添加 hiredis v1.5.0 适配层与测试。
4. 1.0 前做一次依赖许可审计。

## 参考

- [MIT License](https://opensource.org/license/mit)
- [NutSync](https://github.com/gomodb/nutsync)（GPL-3.0-or-later）
- [hiredis v1.5.0](https://github.com/redis/hiredis/releases/tag/v1.5.0)（BSD-3-Clause）
- [qredisclient](https://github.com/uglide/qredisclient)（LGPL-2.1，已归档）
- [REUSE](https://reuse.software/)
