# ADR-0001：主界面采用 Qt Widgets + KDE Frameworks 6

- 状态：Proposed
- 日期：2026-10-18
- 决策者：KeyDis 项目
- 相关：Qt 6、KDE Frameworks 6、KDE HIG

## 背景

KeyDis 是数据密集型桌面客户端，核心界面包括：

- 多级连接树；
- 十万到百万级 Key 的表格与命名空间树；
- 多列、可编辑、可多选的值表格；
- 大文本/JSON/Hex 编辑器；
- 多 tab、可停靠面板、复杂快捷键；
- 需要与 KDE 的菜单、工具栏、配色、文件对话框、钱包、通知深度集成。

候选方案：

1. Qt Widgets + KDE Frameworks 6；
2. Kirigami / Qt Quick；
3. Web 技术 + WebView / Electron；
4. Qt Widgets，但不使用 KF6，仅做 Breeze 风格。

## 决策

**采用 Qt Widgets + KDE Frameworks 6 作为 v1 主界面技术栈。**

- 主窗口：`KXmlGuiWindow`
- 菜单/工具栏：KXmlGui + `KActionCollection`
- 设置：KConfig
- 文件：KIO
- 秘密：KWallet
- 文本高亮：`QPlainTextEdit` + KSyntaxHighlighting
- 模型/视图：`QAbstractItemModel` + 自定义 delegate
- 不使用 Kirigami/QML 构建主界面，不引入 Electron/WebView。

## 理由

1. **数据密集场景成熟**：Qt Widgets 的 model/view、委托、选择模型、撤销栈和键盘导航更适合大表格与复杂编辑。
2. **KDE 原生应用主流**：Dolphin、Kate、Konsole 等使用 Widgets；KDE HIG 并不要求 QML。
3. **性能可控**：无 Web 运行时；可精确控制虚拟化、绘制和线程。
4. **无障碍与输入法**：Widgets 的辅助功能、输入法、快捷键生态成熟。
5. **交付风险低**：团队可用现有 C++/Qt 经验；不需要同时维护 QML 与 C++ 两套状态。
6. **KDE 集成直接**：KXmlGui、KConfig、KWallet、KIO 等本身就是 Widgets 生态的一等公民。

## 后果

### 正面

- 大 Key 空间、复杂表格和编辑器实现更直接。
- 启动快、内存低，符合差异化定位。
- KDE 集成成本低，行为与用户其他 KDE 应用一致。
- 测试可用 QtTest 直接操作 Widgets。

### 负面

- 视觉上不如 QML 动画丰富；需要通过布局、图标、配色和细节达到 KDE 质感。
- 高 DPI/触摸体验需要额外打磨。
- 未来若做移动端/Plasma widget，不能直接复用 UI 层。
- 自定义绘图（图表、热力图）需要 QPainter 实现。

## 备选方案

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| Kirigami/Qt Quick | 现代 KDE 视觉、动画、触摸友好 | 大表格/文本编辑复杂；QML/C++ 状态同步；无障碍与输入法风险 | 不用于 v1 |
| Electron/WebView | 开发快、生态多 | 内存/启动/桌面一致性差；与“原生 KDE”定位冲突 | 排除 |
| Widgets 无 KF6 | 依赖少、跨平台 | 菜单/设置/钱包/文件/通知需自造；不够 KDE | 排除 |
| 混合：QML 欢迎页 + Widgets 主界面 | 视觉新鲜 | 两套技术栈、构建/主题/测试复杂 | 暂不采用 |

## 后续

- P0 建立 KXmlGui 壳与 KDE HIG 基线。
- P5 做 HIG/无障碍审计；必要时用 QPainter 实现图表/热力图。
- 若未来做移动端，单独评估 Kirigami 前端，核心层复用。
