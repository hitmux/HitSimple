# 诊断与调试可观测性改进计划

## 状态

本计划从诊断体验和调试可观测性出发，取代已完成的标准库 provider
计划。Phase 0 和 Phase 1 已完成并通过本地完整回归；Phase 2 已按已确认的
checked 默认 policy 实现并完成本地定向验证，仍等待 Linux、macOS 和 Windows
GitHub Actions 的实际 runtime 回归。Phase 3 在该验收前不提前开始或宣称完成。

## 目标

让 HitSimple 的编译失败、静态安全失败、checked runtime 失败和原生调试
形成一致、可定位且可被工具消费的链路：

```text
源文件 / $include
  -> SourceRange
  -> compiler diagnostic / VS Code Problem
  -> checked runtime failure 或 native debugger
```

完成后，用户应能从 CLI 或 VS Code 看到错误的文件、行、列和源码上下文；
已知的 checked runtime 失败应在选定的 source-location policy 下指出触发
检查的 HitSimple 调用点。这个目标不改变 `Standard.md` 定义的语言语义、
View 模型或 safety-mode 合同。

## 当前基线

- `Diagnostic` 保存一个 primary `SourceRange` 和零个或多个关联 label。默认
  human 首行仍为 `hsc: file:line:column: stage: severity: message`；关联位置以
  后续 `note` 输出，JSON 使用 stderr NDJSON。
- AST 和 HIR 都保留 source range。sema 的 `CurrentRangeGuard` 会将当前 AST
  range 复制到新建 HIR 节点；LLVM codegen 在 `-g` 时已用 HIR range 生成
  `DILocation`。
- codegen 的 `addDiagnostic()` 现在会使用 HIR statement/expression lowering
  和 static safety traversal 的当前 range；没有源节点的 target/toolchain/
  internal 错误仍保持未定位。
- `Diagnostic::format()` 继续只输出稳定的单行 primary diagnostic。stderr 的
  human renderer 会在其后按需显示原始单行源码与 caret/underline；多行 range
  或不可读取文件安全回退为首行。
- 预处理器诊断使用 `preprocessor` stage。VS Code `$hsc` Problem Matcher 已接受
  `preprocessor` 和 `note`，会捕获带有文件、行和列的 primary/file-level
  diagnostics；扩展任务保持 human 格式。
- `--timing`、`--timing-json=<path>`、`--dump-tokens`、`--dump-ast`、
  `--dump-hir`、`--emit-llvm` 和 `-g` 已可用。`-g` 的 Linux GDB 回归已验证
  函数、栈帧和局部变量；局部 View 在 debugger 中保持为底层字节存储，而非
  C-like 静态类型。
- checked runtime 以状态码 `120` 退出。每个 compiler-generated checked runtime
  call 在调用前写入 HIR 的规范化 source path、line 和 column；runtime 以
  thread-local context 保存该位置，并在可用时输出
  `hitsimple runtime error: <message> at file:line:column`。无 compiler context
  的 runtime 调用安全回退为原有消息。`extern` global、FFI 裸地址和 file
  handle 的既有追踪边界不在本计划中扩大。

## 稳定边界

1. `Standard.md` Chapter 18 的诊断和 checked-mode 要求仍是行为边界。改进
   报告内容和定位，不得吞掉或延后既有必需诊断。
2. 默认 human diagnostics 的首行继续保持
   `hsc: file:line:column: stage: severity: message`。源码摘录、关联 note
   等附加行写在首行之后，避免破坏 CLI 脚本和现有 VS Code matcher。
3. 不为改善 GDB/LLDB/VS Code 显示而在 AST 或 HIR 中引入传统静态类型系统。
   `bytes`/View 的调试表示必须保留内存和显式解释语义。
4. 本计划不引入 LSP、定制 Debug Adapter、远程调试或跨目标调试。现有
   `ms-vscode.cpptools` 路线继续分别使用 `cppdbg + GDB`、`cppdbg + LLDB`
   和 `cppvsdbg + PDB`。
5. 已确认 runtime source-location policy B：所有 checked 二进制默认嵌入
   规范化 source path、line 和 column。路径是产物元数据，发布前需评估其泄露
   风险；`unchecked` 和 `static-checked` 不插入 source-location call。
6. 当前工作区存在与本计划无关的未提交编译器改动。实施时将诊断工作保持
   为独立、可审查的变更集，不覆盖或重排这些改动。

## 分阶段实施

### Phase 0：带范围的编译诊断与源码摘录

**状态：已完成。** `Stage::Preprocessor`、codegen range propagation 和 stderr
human renderer 均已落地，`Diagnostic::format()` 与 stdout action 合同保持不变。

已实现内容：

1. `src/preprocessor/` 产生 `preprocessor` stage；`vscode/hitsimple` 的 `$hsc`
   matcher 和 Node tests 已覆盖该 stage。
2. `LlvmEmitter` 在 HIR lowering 与 static safety traversal 中维护当前
   `SourceRange`，static-checked `calloc` overflow 等 codegen diagnostics 会附加
   正确的源文件、行和列。
3. `src/diagnostic/` 的 renderer 仅在 primary line 后显示原始单行和
   caret/underline，支持 tab 对齐；不可读取文件不会产生二次错误。

**验收条件：**

- 已验证 lexer、parser、sema、preprocessor 和 static-checked codegen CLI 回归，
  均断言 primary location、stage 和源码摘录；`$include` 样例摘录 include 文件。
- 已验证不可读取 source excerpt 安全回退；`--emit-llvm` 与 `--timing` stdout
  对照保持一致，失败 action 不将 renderer 写入 stdout。
- 已运行 `cmake --build build --parallel`、`./build/hsc_unit_tests`（413 tests,
  0 failures）、`npm test`（25/25）和
  `ctest --test-dir build --output-on-failure --parallel 4`（281/281）。

### Phase 1：关联诊断、文件级诊断与结构化输出

**状态：已完成。** `Diagnostic` 现有 primary range 和关联 label；human 仍以
稳定 primary 首行输出，并为每个 label 紧随一个 source-aware `note`。重复
local/global/function/template/label 均把 primary 放在重定义处、把 note 放在
首次声明处。

已实现内容：

1. 缺失 `main` 和跨翻译单元 C external ABI 冲突作为 file-level sema
   diagnostics 附到相关 input 的 `1:1`；ABI 冲突还附带首次 external
   declaration note。无输入 CLI 错误仍是未定位的 `hsc: missing input file`。
2. `--diagnostic-format=json` 在 stderr 输出每条 compiler diagnostic 一个
   NDJSON object：`severity`、`stage`、`message`、`primary` begin/end 或 `null`
   以及 `related` labels。它不改变 human 默认、stdout action，或独立的
   `--timing-json=<path>` 文件。
3. `README.md`、`USAGE.md` 与 extension `additionalArgs` 配置说明了 JSON
   仅供直接 CLI 消费；`$hsc` 继续消费 human diagnostics。Node 与 Extension Host
   回归已把 missing `main` 验证为可捕获的 Problems marker。

**验收结果：**

- unit tests 覆盖 local/global/function/template/label 的 primary 与首次声明
  label，以及 JSON range/null serialization。
- CLI/CTest 覆盖 lexer、include-origin parser、多个 sema records、static-checked
  codegen、file-level `main`、C ABI 冲突、无输入和 timing JSON 隔离。
- 已运行 `cmake --build build --parallel`、`./build/hsc_unit_tests`（416 tests,
  0 failures）、`npm test`（25/25）和
  `ctest --test-dir build --output-on-failure --parallel 4`（289/289）。

### Phase 2：checked runtime source-location

**状态：已实现并通过本地定向验证；跨平台 Actions 待运行。** 已确认
policy B：所有 `--checked` 二进制默认写入规范化 runtime source location。
该位置会增加产物中的 source path、line 和 column 元数据；`USAGE.md` 已说明
这一分发隐私影响。

已实现内容：

1. 复用 HIR range，在每个可能进入 `hs_fail` 的 generated checked runtime 调用
   前设置调用点上下文；覆盖直接 memory check、算术 overflow、字符串、格式、
   文件和分配 wrapper，以及 runtime frame/object registration。
2. 在 `runtime/` 使用跨 Linux/macOS/Windows 的 thread-local context，防止
   多线程 native interop 互相覆盖失败位置；无 context 时保留原有
   `hitsimple runtime error: <message>` 输出。
3. 保持 `unchecked` 和 `static-checked` 不插入 runtime context 写入；保持
   runtime error 的类别、状态码 `120` 和既有安全边界。
4. CTest 覆盖动态 out-of-bounds、double free、动态 scan format、
   `abs(iN_min)` 的原因与精确 file/line/column；另覆盖无 context fallback 和
   checked-only LLVM IR policy。release workflow 的 Linux aarch64 与 Windows
   runtime steps 已增加相同断言；macOS 和 Linux x86_64 运行完整 CTest。

**本地验收结果：**

- 已运行 `cmake --build build --parallel`、新增 codegen unit test，及 3 项
  定向 CTest；动态案例和 IR policy 均通过。

**剩余验收：**

- Linux、macOS 和 Windows 的 GitHub Actions 必须实际运行各自的 runtime
  source-location 回归后，才可将 Phase 2 标为完成或声明跨平台支持。

### Phase 3：原生调试回归加固

**状态：待开始。** 不新增调试器路线，验证现有 `-g` 和 VS Code integration
在诊断改进后仍保持可用。

1. 扩展 native debug tests，覆盖 source breakpoint、`helper`/`main` stack、
   local View 存储、include source mapping 和相邻表达式的 stepping。
2. 将 Phase 2 已实现的 checked runtime failure 纳入 `-g` 路线验证；不将
   runtime 报错本身误称为 native stack trace。
3. 继续按平台验证现有适配器：Linux `cppdbg + GDB`、macOS `cppdbg + LLDB`、
   Windows `cppvsdbg + PDB`。只声明经过真实 debug-session 验证的平台。

**验收条件：**

- Linux GDB batch test 继续验证 DWARF、断点、stack 和 locals。
- 对 extension 改动运行 Node tests、Extension Host tests、VSIX package/install
  smoke；跨平台结论以对应 GitHub Actions 实际结果为准。
- `-g` 关闭时 LLVM IR 不带 debug metadata；开启时保留 `DILocation`、
  `DISubprogram` 和局部存储声明。

## 风险与暂缓事项

- human renderer 必须尊重 `$include` 的 original source origin、不可读取文件、
  tab 对齐和多行 range；任何失败都只能降级显示，不能覆盖原诊断。
- 人类输出首行和 JSON 是不同合同。修改首行、stdout dump 或 `--timing-json`
  schema 会影响现有脚本和扩展，不在本计划范围内。
- runtime source context 涉及 compiler/runtime internal ABI、优化器顺序和三种
  原生平台；当前仅有 Linux 本地证据，Phase 2 尚不能宣称完成。
- native debugger 中以字节数组表示 View 是既定语义映射。将来如需模板感知
  展示，应作为 debugger visualizer 独立提案，不能改变编译器类型模型。
- LSP、远程调试、custom launch configuration、cross-target debugging 和完整
  runtime ownership tracking 均暂缓。

## 立即下一步

运行 release workflow 中 Linux、macOS 和 Windows 的 runtime source-location
回归并核实结果。仅在三平台均有实际通过证据后，将 Phase 2 标为完成并进入
Phase 3。
