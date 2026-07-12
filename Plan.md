# HitSimple VSCode 扩展计划

## 当前定位

HitSimple 已有可运行的 C++20 编译链和分层回归；`Standard.md` 仍是语言行为依据。本计划只覆盖 `vscode/hitsimple/` 的编辑器支持，以及为编译诊断定位所需的最小 `hsc` 输出改进，不改变语言语义、编译管线结构或 C compatibility 范围。

扩展现在同时包含声明式语言支持和运行时代码：`.hs`、`.hsh`、`.hsi` 的 TextMate grammar、语言配置、snippets、`$hsc` Problem Matcher，以及 `hitsimple.buildCurrentFile`、`hitsimple.runCurrentFile` 两个命令。Phase 0–4 已完成，当前交付物是本地 VSIX；LSP、Marketplace 发布、图标、许可证和独立 README 仍属于后续独立决策。

## 目标与边界

### 本轮目标

1. 让高亮、自动缩进和 snippets 与当前 lexer、parser 及 `Standard.md` 保持一致。
2. 让 `hsc` 的带位置诊断稳定进入 VS Code Problems，并明确无位置诊断的呈现边界。
3. 提供可配置的编译、运行入口，避免用户为每个工作区重复手写任务定义。
4. 为后续 LSP 留下稳定的诊断和任务边界。

### 暂不纳入

- 更改 `Standard.md` 定义的语言语义。
- C compatibility 的编辑器级语义支持。
- 自动格式化、调试适配器、代码补全、跳转定义和实时语义分析。
- Marketplace 发布、图标、许可证调整和独立扩展 README。
- Windows、macOS 和最低 `engines.vscode` 版本的完整兼容矩阵。

## 已知问题与固定决策

| 主题 | 当前事实 | 执行决策 |
| --- | --- | --- |
| 词法与高亮 | 数字形式、完整 typed operators、`template`、`impl`、`op`、`as`、`self`、`mut` 及声明名称 scope 已与 lexer 对齐。 | TextMate 只表达 token 形状，宽度和语义合法性继续由 `hsc` 判断；真实 Oniguruma 回归约束 grammar 漂移。 |
| Problems | `$hsc` 支持相对路径、绝对路径和 include 来源，使用 `allDocuments + autoDetect`。 | 有真实 `SourceRange` 的诊断进入 Problems；没有唯一源码实体的诊断只留在任务终端，不伪造文件位置。 |
| `.hs` 关联 | `.hs` 与 Haskell 扩展存在后缀冲突；VS Code 1.128.0 的当前组合默认选择 HitSimple，但扩展优先级不是跨版本合同。 | 推荐工作区显式设置 `"files.associations": {"*.hs": "hitsimple"}`。 |
| 命令执行 | Build/Run 使用 `ProcessExecution` 和 argv，不经过 shell。 | mode、output 和 action 参数由扩展控制；`additionalArgs` 只接收独立 argv 项，并拒绝覆盖受控参数。 |
| 输出安全 | 相对输出目录必须留在工作区内，构建前删除 stale output。 | Run 只消费本次成功 Build 的确切产物；编译失败、产物缺失或不可执行时不启动 Run。 |
| 工作区能力 | Build/Run 会执行本地或远程工作区中的编译器。 | 只接受受信任的 `file` 或受支持的 `vscode-remote` 工作区；untitled、workspace-less、virtual workspace、`.hsh` 和 `.hsi` 均明确拒绝。 |
| 发布范围 | 本地 VSIX 已可安装，manifest 仍缺少 `repository`，仓库未提供扩展许可证文件。 | 当前只交付本地 VSIX；Marketplace 元数据和许可证由独立发布任务处理。 |

## 阶段计划

### Phase 0：建立扩展验证基线（已完成）

**完成状态**

- Node 测试使用真实 `vscode-textmate`、`vscode-oniguruma` 和锁定的 `onig.wasm`，覆盖 manifest、grammar、language configuration、snippets 和 Problem Matcher。
- `test/fixtures/syntax-surface.hs` 与真实 `hsc` 用于约束语法表面和诊断格式，不以手写正则样例代替编译器输出。

**验收结果**

- 当前 `npm test` 汇总 Phase 0–3 的 Node 回归：20 tests，20 passed。
- C++ 单元测试和完整 CTest 继续作为编译器诊断改动的回归边界。

### Phase 1：语言表面与编辑体验对齐（已完成）

**完成状态**

- grammar 已覆盖当前数字形式、数字分隔符、完整 typed-operator matrix、六个专用关键词和 `struct`、`template`、`impl` 声明名称 scope。
- 大括号和预处理条件块已有缩进规则；`{`、`[`、`(` 在 string/comment 中不自动闭合。
- 新增模板、`impl op`、模板方法、`extern` variable、`set`、显式长度和模板声明等 11 个 snippets；未加入标准模式会拒绝的 `mut self` snippet。

**验收结果**

- 真实 Oniguruma 覆盖 fixture、`examples/comprehensive_project.hs` 和 `tests/cases/run/user_template_ops.hs`。
- Extension Host 使用贡献 snippet 名称插入 `main function`，并通过真实 `type` 命令验证 `{` 和 `$ if` 后换行缩进四格。
- 主题像素表现和 UI scope inspector 未自动化验证；当前结论限于 grammar tokenization、语言关联和编辑行为。

### Phase 2：编译诊断到 Problems 的闭环（已完成）

**完成状态**

- 带位置诊断统一为 `hsc: <file>:<line>:<column>: <stage>: <severity>: <message>`；原生 parser、AST 和 sema 传递真实 `SourceRange`，include 的 `lineOrigins` 保留到下游。
- `--dump-tokens` 非法 token 使用统一 lexer 诊断。HIR linkage/ABI override、codegen invariant、缺少或重复 `main` 等没有唯一源码实体的错误继续保持无位置。
- `$hsc` 使用 `applyTo: "allDocuments"` 和 `fileLocation: "autoDetect"`。

**验收结果**

- Node 回归覆盖相对路径、绝对路径、include lexer/parser、sema 和无位置诊断。
- Extension Host 通过 `languages.getDiagnostics` 验证当前文档 sema marker、include `.hsi` marker 的 URI/range/severity/source/message，并确认无位置 missing-main 不产生 marker。

### Phase 3：可配置的 Build 与 Run 工作流（已完成）

**完成状态**

- 已实现 `hitsimple.buildCurrentFile`、`hitsimple.runCurrentFile`，以及 `hitsimple.compilerPath`、`hitsimple.mode`、`hitsimple.outputDirectory`、`hitsimple.additionalArgs`。
- `buildPlan.js` 负责 argv、mode、跨平台输出名和路径边界；`executable.js` 在启动 Task 前解析绝对路径、相对路径或 Extension Host `PATH`，缺少编译器时直接返回可操作错误。
- `taskRunner.js` 使用 `TaskExecution` 身份匹配结束事件，同时处理进程结束与未启动进程的 Task 结束边界。
- Build 前保存 dirty document、清理 stale output、创建目标目录并验证普通文件和 executable 权限；Run 只使用该次 Build 返回的产物。

**验收结果**

- Extension Host 中 `--unchecked`、`--static-checked`、`--checked` 均由真实 `hsc` 成功构建，Run 正常退出。
- 真实失败路径覆盖：编译失败不启动 Run、缺少 compiler、输出目录被普通文件阻塞、无位置诊断不产生 Problems marker。
- Node mock 覆盖 untitled、错误 language mode、workspace-less、untrusted、virtual workspace、`.hsh`、`.hsi`、Windows `.exe`、路径逃逸和受控参数拒绝。

### Phase 4：本地安装与发布前验证（已完成）

**完成状态**

- `package-lock.json` 锁定 `@vscode/test-electron@3.0.0` 和 `@vscode/vsce@3.9.2`；测试 runner 固定 VS Code 1.128.0，并允许通过 `HSC_PATH`、`VSCODE_TEST_VERSION` 覆盖。
- `.vscodeignore` 排除依赖、测试、lockfile、`.vscode-test/`、`dist/` 和嵌套 VSIX；`vsce ls --tree` 只列出运行所需文件。
- Extension Host 验证语言关联、命令自动激活、三种 mode、Build/Run、Problems、include marker、无位置诊断、snippet、缩进和失败 gating。
- Haskell 冲突使用 `haskell.haskell@2.8.2`、`haskell.language-haskell@3.8.0` 与当前 VSIX 复验；显式 `files.associations` 可稳定选择 `hitsimple`。

**验收结果**

- VS Code CLI 1.128.0 在隔离 profile 中完成 install → list → uninstall → reinstall，均识别 `hitmux.hitsimple-vscode@0.1.0`。
- 当前产物：`vscode/hitsimple/dist/hitsimple-vscode-0.1.0.vsix`，14,053 bytes，SHA-256 `f6c930d76b884a7f8c9fc86b5e425720f6db05c04d932ac68b34ffe69ce11180`。
- VSIX 共 11 个归档条目：2 个 VSIX 元数据和 9 个扩展运行文件；五个 `src/*.js` 的归档 SHA-256 与工作树一致。
- `vsce` 仅报告缺少 `repository` 和 LICENSE 的发布元数据警告，不影响本地安装；当前不据此声称满足 Marketplace 发布要求。

## 后续决策点：LSP

LSP 不属于本计划的未完成项。只有以下条件成立时再单独立项：

1. `hsc` 能以稳定的机器可读格式输出 diagnostics 和 source range。
2. 实时诊断、completion 或 definition 的具体用户场景已确认。
3. 协议、进程生命周期、增量文档同步和跨平台启动方式有独立设计。

届时优先实现 diagnostics，再按需求推进 hover、definition、completion。formatter 和 debugger 继续独立评估。

## 最终验证

本计划完成时的实际验证结果：

- `cmake -S . -B build`：通过；LLVM 19.1.7、Bison 3.8.2、re2c 和 clang 已识别。
- `cmake --build build --parallel 8`：通过。
- `./build/hsc_unit_tests`：362 tests，0 failures。
- `ctest --test-dir build --output-on-failure --parallel 4`：187/187 passed。
- `cd vscode/hitsimple && npm test`：20/20 passed。
- `cd vscode/hitsimple && xvfb-run -a npm run test:extension`：exit code 0，输出 `HitSimple Extension Host assertions passed`。
- `cd vscode/hitsimple && npm run verify:vsix`：只列出 9 个扩展运行文件。
- 当前 VSIX 的归档检查、源码 hash 对比及隔离 profile 安装/卸载/重装：通过。

## 保留限制与后续独立事项

- 未在 VS Code 1.90、Windows 或 macOS 运行 Extension Host/VSIX 矩阵；当前真实结果只覆盖 Linux x64 + VS Code 1.128.0。
- untrusted 和 virtual workspace 的拒绝逻辑由 manifest 与 Node mock 覆盖；测试 runner 会关闭 Workspace Trust，因此没有真实 untrusted Extension Host smoke。
- 未自动化验证主题像素、状态栏显示或手工点击 Problems 面板；Problems 结论来自官方 `languages.getDiagnostics` API。
- 如需 Marketplace 发布，另行补齐 `repository`、LICENSE、README、图标和发布账户流程。
- 如需 LSP、formatter、debugger 或 C compatibility 编辑器语义支持，分别建立独立计划，不回填到本轮已完成范围。
