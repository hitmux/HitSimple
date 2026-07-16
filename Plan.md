# HitSimple GPU-native 编译研究与实施计划

## 文档定位

本文是 HitSimple 下一阶段的活动执行计划，覆盖语言标准、编译器中端、GPU 分析与优化、常驻编译服务、基准和验收。它同时记录技术判断及其理由，用于约束后续实现，避免在没有数据时把“GPU 可运行”误写成“GPU 能加速”。

`Standard.md` 继续是语言行为的权威来源。本计划可以提出和实施标准演进，但任何实现都必须先有明确的标准合同；现有测试通过只说明当前实现没有出现已覆盖回归，不代表标准条款已经完整实现。

当前计划不承诺 GPU 路径一定进入默认编译流程。研究必须允许得出“某类 workload 不值得 GPU 化”的结论，并保留 CPU fallback。

## 总体判断

### 结论

HitSimple 适合研究一套 **GPU-native 编译中端**：CPU 保留预处理、解析、名称收集、模板解析和主要语义诊断；GPU 批量执行函数级与全程序数据流分析、高成本优化和搜索算法；CPU 或 LLVM 继续负责最终目标代码生成、链接和诊断呈现。

HitSimple 的价值不在于复刻一个运行在 GPU 上的 Clang，而在于利用自身的显式字节语义，设计可显存常驻、可增量更新、可确定性并行分析的 IR，并探索传统编译器因成本过高而无法默认执行的安全分析和优化。

### 为什么值得做

1. HitSimple 的核心语义是存储对象、字节范围、View、解释模板和生命周期，而不是复杂的隐式类型推导、继承或动态派发。这些事实可以压缩成整数 ID、区间、bitset 和稠密表。
2. 普通函数参数和返回值长度大量静态确定，模板操作静态解析，隐式跨模板转换被禁止，适合生成稳定的函数摘要和批量分析输入。
3. `static-checked` 和 `checked` 已经要求边界、生命周期、初始化状态和运行时错误分析。GPU 可以用于提升分析覆盖率，而不只是缩短现有轻量 Pass。
4. 论文表明 GPU 对百万级指令、跨函数 fixed-point 和高成本搜索算法有真实潜力；收益随规模增长，并高度依赖数据布局、warp divergence 控制和成本模型。
5. HitSimple 是实验语言，可以适度调整标准，为可分析性提供更明确的 effect、alias 和求值合同，而不需要兼容 C++ 的全部历史负担。

### 为什么不能直接全面 GPU 化

1. 当前最大示例仍很小，普通本地编译无法摊销 GPU context、传输和同步成本。
2. 当前 HIR 是 `virtual` 对象、`unique_ptr` 和嵌套树，直接复制到显存会产生不规则访问和较高内存放大。
3. 预处理、错误恢复、名称诊断、复杂源码位置映射和链接具有强顺序性或较低算术强度。
4. GPU 算法的正确性、确定性、显存容量和长函数尾部效应可能比 kernel 编写本身更难。
5. LLVM、CPU 多线程、增量缓存和文件级并行已经能获得低风险收益，GPU 必须与这些基线比较。

### 当前主观评分

| 方向 | 判断 | 理由 |
|---|---:|---|
| GPU-friendly `FlowIR` | 9/10 | 与 HitSimple 的字节/View 模型匹配，即使 GPU 失败也能改善中端边界。 |
| GPU 批量数据流与 static-checked 分析 | 8/10 | 问题可表达为 CFG、区间、bitset 和 fixed point，且能提升能力而非只优化时间。 |
| 常驻 GPU 编译服务 | 8/10 | 是摊销初始化、缓存 kernel 和维持显存常驻 IR 的必要形态。 |
| 高成本搜索优化 | 8/10 | 论文已证明 GPU 能让 ACO 一类算法更接近实用，但必须选择性启用。 |
| 当前小程序编译提速 | 2/10 | 当前工作量过小，外部 LLVM/Clang 后端和进程开销占比更高。 |
| 完整前端、后端全部运行在 GPU | 5/10 | 研究价值存在，但语法限制、内存放大、正确性和工程成本很高。 |
| 形成独立研究成果 | 8/10 | “显式字节语义 + 常驻 IR + 确定性 GPU 分析”有区别于通用编译器的主题。 |

评分是研究优先级判断，不是验收结果；后续必须由 benchmark 更新。

## 论文证据与采用方式

论文保存在 `research/gpu-compiler/papers/`：

- `parallel-lexing-parsing-semantic-analysis-gpu.pdf`
- `parallel-code-generation-gpu.pdf`
- `gpu-accelerated-fixpoint-algorithms-cc19.pdf`
- `instruction-scheduling-gpu-on-gpu-cgo24.pdf`

本计划采用以下结论：

1. Pareas 前端说明完整 GPU 编译器会受到初始化延迟、语法限制和显存放大的影响。其 GPU 初始化约为秒级，小输入的分发与同步开销会吞掉收益。
2. Pareas 后端说明宽浅树适合批处理，深窄树和超长函数会形成串行尾部；register allocation 的顺序 lifetime analysis 会成为瓶颈。
3. CC 2019 fixed-point 论文在大型输入上把部分数据流分析加速到数十倍，端到端最高节省 26.5%；最小输入会因为 CFG 转换和传输成本减速。
4. CGO 2024 instruction scheduling 论文说明 GPU 最适合承载昂贵算法。GPU ACO 相对 CPU ACO 降低 21% 总编译时间，但相对生产级快速 heuristic 仍增加 15.1%，因此必须先用 heuristic 和 cost model 筛选区域。
5. 论文中的最高 speedup 不能直接外推到 HitSimple。必须包含 CPU 多线程、数据传输、IR 转换、验证和 cold/warm 两种 GPU 状态。

## 当前实现基线

### 已有管线

```text
source
-> mcpp preprocessor
-> lexer / parser
-> AST
-> sema
-> pointer-based HIR
-> LLVM IR text
-> temporary .ll files
-> external Clang backend and linker
```

当前事实：

- 每个输入 translation unit 依次执行 parse、sema 和 LLVM emission。
- HIR 表达式和语句主要通过继承、`virtual`、`unique_ptr` 和嵌套 `vector` 表达。
- `FlowIR` 已作为独立、扁平的 analysis 中端落地；当前 schema v2 包含 function effect contract 摘要，只由 `--dump-flow-ir`、`--flow-ir-stats`、`--dump-flow-ir-analysis` 和显式的 `--dump-flow-ir-gpu-analysis` 构建与验证，现有 HIR -> LLVM 路径保持不变。
- LLVM emitter 依次声明和生成函数，没有独立的 HitSimple optimization Pass 层。
- 可执行文件路径会先生成文本 LLVM IR 和临时 `.ll`，再由外部 Clang 读取并完成目标代码生成与链接。
- 现有 AST、HIR、LLVM codegen、C compatibility、runtime 和分层测试必须继续作为正确性基线。

### 当前性能认识

本轮研究前的粗测中，`examples/comprehensive_project.hs` 的前端到 LLVM IR 大约为 0.05–0.10 秒，完整编译大约为 1.18–1.91 秒。该样本只有约 9 KB，不能用于证明大型 batch 的 GPU 收益，只能说明当前普通 workload 不应默认启动 GPU。

正式结论必须由 Phase 0 的阶段计时和可复跑 benchmark 取代这组粗测。

### 当前验证状态

本轮创建计划时启动过完整重建，但按用户要求停止，没有取得完整 build、unit tests 或 CTest 结果。本计划不得引用这次部分构建为通过证据。

## 研究问题

本项目需要回答以下可证伪问题：

1. 能否设计一套保留 HitSimple 字节/View 语义、同时适合 CPU 和 GPU 批处理的紧凑 IR？
2. 哪些分析在包含转换、传输、同步和验证后仍能超过 CPU 多线程版本？
3. `static-checked` 能否借助 GPU 执行更精确的跨函数范围、生命周期和初始化状态分析？
4. 显存常驻和增量更新是否能把普通 CLI 的秒级 context 成本摊薄到可接受范围？
5. 自动 cost model 能否稳定避开小函数、超长函数和低收益 Pass？
6. GPU 并行执行能否保持固定点结果、诊断和 rewrite 的确定性？
7. 高成本算法产生的代码质量收益能否抵消额外编译时间？
8. 如果 GPU 路径达不到收益阈值，FlowIR 和 CPU analysis 是否仍值得保留？

## 范围与非目标

### 纳入范围

- 对 `Standard.md` 做硬件中立、可验证的求值、effect 和 alias 合同演进。
- 新建扁平、稳定 ID、SoA/CSR 友好的 `FlowIR`。
- 建立 CPU reference Pass、GPU Pass、差分验证和自动 fallback。
- 支持 warm、cold、resident、incremental 四类性能测量。
- 优先研究 liveness、View range、对象生命周期、初始化字节、函数 effect summary、SCCP、DCE 和 GVN。
- 后期研究 ACO、equality saturation 或其他高成本搜索优化。
- 保留 LLVM 作为首个目标代码后端。

### 暂不纳入

- 第一阶段不实现 GPU lexer、GPU parser 或 GPU 名称解析。
- 第一阶段不替换 LLVM instruction selection、register allocation 和 object emission。
- 不为了并行 parser 强迫核心语法满足 LLP/OPG 等特定文法类别。
- 不要求用户机器必须有 GPU。
- 不承诺 OpenCL、Vulkan、HIP、SYCL 或其他单一厂商 API 永久成为公开合同。
- 不把 `FlowIR` 内存布局、daemon 协议或 kernel 策略写入语言标准。
- 不在没有 benchmark 证据时删除 CPU 路径。
- C compatibility 继续是受限翻译层，不因 GPU 研究扩大兼容承诺。

## 固定架构决策

### CPU 前端继续作为语义入口

预处理、lexer、parser、AST 和主语义分析继续运行在 CPU。原因是这些阶段包含错误恢复、名称表、源码位置和不规则控制，当前 workload 也不足以摊销 GPU 化成本。

CPU 前端必须输出已经完成以下工作的 HIR/FlowIR 输入：

- 名称绑定和顶层 `impl` 合并；
- 模板与 `impl op` 静态解析；
- 参数和返回 View 长度确定；
- C compatibility 到核心语义的翻译；
- 源码范围和 diagnostic identity 保留。

### `FlowIR` 是新的中端边界

当前 HIR 保留为迁移期语义表示。新增 `FlowIR` 后，先用于分析；当 verifier、差分测试和 LLVM lowering 稳定后，再让 LLVM 后端只消费 `FlowIR`。在完成迁移前不得删除现有 HIR 路径。

选择自定义 `FlowIR` 而不是直接采用 MLIR 作为显存格式，理由如下：

- MLIR 的 pass 约束和隔离思想值得借鉴，但其 C++ operation 对象本身不是 GPU-resident SoA。
- HitSimple 需要显式表达字节范围、View 模板、对象生命周期和 checked effect，这些语义比通用 dialect 更集中。
- 自定义紧凑表示可以稳定控制 device memory、serialization 和 ID。
- 后续可以提供 MLIR/LLVM 导出层，不把 MLIR 变成第一阶段依赖。

### CPU reference 和 fallback 是强制要求

每个 GPU Pass 必须先有行为一致的 CPU reference 实现。GPU 不可用、数据规模过小、显存不足、kernel 失败、验证失败或 cost model 判断无收益时，必须回退 CPU，且不得改变程序语义。

### GPU backend 不绑定语言标准

内部通过统一 backend 接口支持 `CPU`、`OpenCL`、`HIP` 或其他实现。首个 GPU backend 根据实际开发硬件和工具链选择；选择结果属于构建和部署合同，不属于 `Standard.md`。

### 常驻服务是最终产品形态

单次 `hsc` 进程创建 GPU context 不作为默认产品路径。最终形态是常驻 `hscd` 或等价服务，负责设备 context、kernel cache、项目 FlowIR、分析摘要和增量更新。原型阶段可以先用单进程 benchmark 保持 context 存活，再实现 IPC。

## `Standard.md` 演进计划

标准修改的目标是提供更强的可分析语义，不是把 GPU 写入语言表面。`Standard.md` 已进入 `1.0.0-Beta.22 Draft`：通用 as-if、无序列关系表达式的并发许可和 `effects(...)` 合同已由前端、完整函数体/调用链验证和 checked runtime instrumentation 闭环；检测边界由实现定义项和 `--target-info` 明示。

### 必须先完成的最小合同

#### 1. 通用 as-if 与可观察行为

扩展第 1 章：实现可以并行、重排、合并或删除计算，只要编译期诊断、运行时错误、View 长度和模板、写入字节、I/O、返回值和终止状态与标准规定等价。

理由：目前第 11.9 节只明确标准模板 fast path 的等价要求，GPU 中端需要覆盖整个程序的通用优化合同。

#### 2. 无序列关系表达式的并发许可

扩展第 8.10 节：没有序列点依赖、没有重叠副作用且不违反生命周期约束的求值，可以按任意顺序或并发执行。程序不得通过 timing 或未定义的副作用顺序观察差异。

理由：现有标准已经不保证部分求值顺序，但尚未明确允许并发执行。补充这一点可支持 CPU/GPU 并行而不改变合法程序结果。

#### 3. 函数 effect contract

合同语法确定为 `effects(...)`，位于普通函数、模板方法、`impl op` 或 `extern` 的签名之后。它描述一次调用的完整 effect 上界，不改变普通参数的 View-value 传递，也不启用 `mut`、borrowed View 或新的 alias 参数语义。

- `pure`
- `readonly`
- `read(object, range)`
- `write(object, range)`
- `allocates`
- `frees`
- `throws`
- `nothrow`
- `noalias(a, b)`
- `io`
- `unknown`

`read`/`write` 以 `addr` 参数或静态存储对象及其范围为对象；`noalias` 只适用于带已声明访问范围的两个 `addr` 参数。`pure` 蕴含 `nothrow`；非 `pure`、非 `unknown` 合同必须显式选择 `throws` 或 `nothrow`。未标注的 `extern` 等价于 `effects(unknown)`；定义体的显式合同必须覆盖其自身及被调函数的已证明 effect。

未标注的定义允许编译器仅为内部分析推导保守摘要，不提供源级承诺。`extern` 的显式合同是对已链接实现的承诺；超出该合同的外部实现属于未定义行为。

#### 4. effect 违反规则

- 所有模式下，可静态证明的显式合同冲突必须产生编译诊断。
- `static-checked` 不得为 effect contract 插入运行时检查。
- `checked` 下可运行时检测的范围、`noalias`、生命周期、`pure`、`readonly` 或 `nothrow` 合同违反必须报告运行时错误。
- `unchecked` 下无法静态证明的合同违反属于未定义行为。

#### 5. 实现定义清单与 EBNF

同步更新第 17、18、19、20 章：补充 FFI effect 默认值、诊断项、运行时检查边界、实现定义项和最终语法。不得只改说明段落而遗漏完整 EBNF。

### 实验成功后再决定的标准扩展

以下能力具有价值，但会明显改变调用和别名语义，不在第一轮直接落地：

- 启用当前保留的 `mut` 参数，定义为可写 borrowed View；
- 增加只读 borrowed View；
- 为 borrowed View 提供显式 `noalias` 或 unique contract；
- 引入对象 region/lifetime 参数；
- 为循环提供可选的独立迭代或 reduction contract；
- 为高成本优化提供 profile/hotness 元数据。

这些扩展必须先通过 FlowIR 和分析原型证明收益，再进入标准，避免为了想象中的 GPU 需求永久增加语言复杂度。

### 明确不进入标准的内容

- GPU、OpenCL、Vulkan、HIP、work-group、kernel、device memory 等硬件词汇；
- SoA、CSR、ID 宽度和 serialization 格式；
- daemon 和 IPC；
- offload 阈值和 cost model 参数；
- 某个 Pass 必须运行在 CPU 或 GPU 的要求。

## `FlowIR` 设计

### 设计目标

- 连续内存和稳定整数 ID；
- CPU/GPU 共用逻辑 schema；
- 输入尽量不可变，Pass 输出 analysis facts 或 rewrite log；
- 支持函数级增量替换；
- 支持确定性 dump、serialization、hash 和差分测试；
- 保留诊断所需的源文件、range 和 semantic identity；
- 不把传统 C-like 类型系统重新引入 HitSimple。

### 计划目录

```text
include/hitsimple/flowir/
src/flowir/
include/hitsimple/analysis/
src/analysis/
include/hitsimple/gpu/
src/gpu/
src/daemon/
tests/flowir/
tests/analysis/
tests/gpu/
tests/benchmark/
```

按职责拆分 schema、builder、verifier、dump、serialization、analysis 和 backend；不得把完整 FlowIR、所有 Pass 或 GPU runtime 堆入一个超大文件。

### 基本表

```text
ModuleTable
FunctionTable
BlockTable
InstructionTable
OperandTable
CfgEdgeTable
CallEdgeTable
ObjectTable
ViewTable
TemplateTable
ConstantTable
SourceMapTable
```

初始实现优先使用 32-bit ID，并对数量溢出产生明确错误；不得静默截断。字符串、模板名和文件名使用 intern table，device 侧只保留 ID。

### 值与内存模型

纯计算值使用 SSA。内存不强制退化成单一 MemorySSA token，而是显式记录对象和字节范围：

```text
objectId
offset or offsetValueId
byteLength or lengthValueId
accessKind: read/write/readwrite
aliasClass
lifetimeEpoch
```

能够静态定位的访问使用 `(objectId, constant offset, constant length)`。动态偏移使用 value ID 和已知区间。裸地址、FFI 地址或无法确认来源的地址进入 `UnknownExternal` alias class。

理由：HitSimple 的语义单位是字节范围和 View。显式 object-slice effect 比把所有内存压成传统类型化 SSA 更能保留语言合同，也更适合 bounds、overlap 和 initialized-bytes 分析。

### View 表示

View 不作为传统类型。每个值或内存访问可引用以下元数据：

```text
templateId
byteLength
valueCategory: lvalue/rvalue
interpretationFlags
objectId/offset when addressable
```

`as`、`set`、标准模板和用户模板操作解析必须在 FlowIR 中保留最终绑定，不允许 GPU Pass 重新执行源码级 overload resolution。

### 控制流与生命周期

- `if`、循环、短路、三元、`goto` 全部降为显式 block 和 edge。
- `throw`、checked failure 和未捕获错误使用显式 exceptional edge。
- 局部对象创建、初始化、作用域退出和销毁形成显式 lifetime 指令或 edge action。
- `return`、`break`、`continue`、`goto`、`throw` 离开作用域时的清理必须在 CFG 中可见。
- 函数内 `static` 初始化需要独立状态和递归初始化边界。

### 不可变 epoch 与 rewrite log

Pass 默认读取 epoch N，输出：

- analysis fact arrays；
- replacement map；
- tombstone bitset；
- inserted instruction records；
- diagnostic records。

统一 commit 阶段按稳定 ID 排序、验证后生成 epoch N+1。不得让多个 kernel 任意修改 pointer graph。

### Verifier

FlowIR verifier 至少检查：

- 所有 ID 范围有效；
- block、edge、predecessor/successor 对称；
- operand/use 关系一致；
- SSA 定义支配使用，phi/merge 规则正确；
- object-slice 不发生整数溢出；
- 静态 View 长度与模板合同一致；
- lifetime edge 完整；
- call/return 数量和 View 元数据一致；
- exceptional edge 和清理路径完整；
- source map 可追溯；
- rewrite 前后保持 module invariants。

GPU 输出必须经过 verifier；验证失败时回退 CPU 并报告内部诊断，不能继续生成目标代码。

## Pass 与 GPU 执行模型

### Pass 接口

每个 Pass 提供：

- CPU reference implementation；
- 可选 GPU implementation；
- 输入 schema 和 required analyses；
- 结果 lattice 或 rewrite contract；
- deterministic comparison；
- cost features；
- memory estimate；
- fallback 条件。

### 第一批 Pass

#### 1. CFG reachability 与 liveness

用途：验证 FlowIR、CSR edge、bitset、fixed point、CPU/GPU 差分和长函数调度。它不是最终创新点，但适合作为基础设施验收。

#### 2. View range analysis

传播对象来源、偏移区间、长度区间、模板和 alias class，用于证明解引用、索引、切片、标准库容量和动态长度约束。

#### 3. Object lifetime analysis

跟踪对象创建、作用域退出、动态分配、释放、escape 和悬垂地址。未知 FFI 地址保持保守，不伪造安全证明。

#### 4. Initialized-bytes analysis

用区间集合或分层 bitset 跟踪已初始化字节，支持局部对象、分支合并、循环 fixed point 和部分写入。

#### 5. Interprocedural effect summary

为函数生成 read/write/alloc/free/escape/throw/noalias 摘要；call graph SCC 内迭代，外部函数按显式合同或保守默认处理。

#### 6. SCCP、DCE、GVN

基础分析稳定后加入常规优化，用于验证 rewrite log、分析失效和 LLVM lowering 一致性。必须与 LLVM 已有优化职责区分，避免只增加重复工作。

### 高成本 Pass

在基础路径通过收益 gate 后，再评估：

- ACO 或其他 search-based instruction scheduling；
- equality saturation；
- superoptimization；
- profile-guided candidate search；
- 全程序精确 pointer/escape analysis；
- 跨函数 bounds elimination。

高成本算法必须先运行便宜 heuristic，只有预测代码质量收益高于成本时才进入 GPU。

### GPU 数据布局策略

- CFG 使用 CSR/offset arrays；
- opcode、operand、range 和 flags 使用 SoA；
- analysis-specific CFG pruning 删除无关指令；
- 根据 opcode 或 transfer function 分组，减少 warp divergence；
- 按函数/CFG/region 规模分桶；
- 短函数批量合并，中函数分配独立 block，超长函数允许 CPU fallback 或专用 kernel；
- fast path 禁止无界 device heap allocation；
- analysis working set 必须可预估，显存不足时分批或回退。

## 确定性与正确性

### 基本原则

GPU 执行顺序可以不确定，最终语义结果必须确定。优先选择单调 lattice 和幂等 join，例如 bitwise OR、min、max、集合并和区间扩张。

### 收敛

- 使用 epoch、双缓冲或显式 changed bitset；
- GPU 可以推测“可能收敛”；
- 最终必须运行确定性 convergence check；
- 若检查发现未收敛，继续迭代或回退 CPU；
- 不把 heuristic termination 当作正确性依据。

### race 策略

第一版优先使用原子幂等 join，避免让结构损坏后再修复。只有 benchmark 证明同步成本成为主要瓶颈时，才研究论文中的 managed race，并必须保留完整验证与重跑路径。

### 稳定输出

- ID 分配规则固定；
- rewrite commit 按稳定 key 排序；
- diagnostics 按 source range、diagnostic code 和 semantic ID 排序；
- dump/serialization 不包含地址或线程顺序；
- 相同输入、配置和 target 的重复编译必须得到相同语义结果；
- 是否要求 object bit-for-bit reproducible 由独立工具链合同决定。

## 常驻编译服务

### 目标结构

```text
hsc CLI
  -> local IPC
hscd
  -> persistent device context
  -> compiled kernel cache
  -> project FlowIR store
  -> function hash and dependency graph
  -> analysis summary cache
  -> CPU/GPU cost model
  -> CPU fallback
```

### 增量更新

- 每个函数、模板、global 和 extern contract 有稳定内容 hash；
- 修改函数只上传变化的 FlowIR segment；
- call graph 或 effect 变化只失效依赖摘要；
- source map 与语义表分离，纯位置变化不应强制重算全部分析；
- schema、compiler version、target、safety mode 和 backend version 进入 cache key；
- daemon 崩溃或版本不匹配时 CLI 自动退回本地 CPU 编译。

### 安全边界

- daemon 只接受当前用户的本地连接；
- 项目 cache 不跨用户共享；
- 不执行来自源代码的任意 GPU kernel；
- 输入 FlowIR 必须在 host 侧验证后上传；
- device 错误不得导致生成未经验证的目标代码。

## 自动成本模型

### 模式

候选 CLI：

```text
--gpu=off
--gpu=auto
--gpu=force
--gpu-device=<id>
--dump-flow-ir
--flow-ir-stats
--timing-json=<path>
```

名称在实现前确认。`off` 永远可用；`force` 仅用于实验和诊断；默认 `auto` 只有在收益 gate 稳定后才考虑启用。

### 输入特征

- active instruction count；
- CFG edge 和 join count；
- 函数/region size histogram；
- opcode distribution；
- 预计 fixed-point iterations；
- fact density 和 bitset 大小；
- device resident 命中率；
- host/device transfer bytes；
- GPU occupancy 和历史 kernel 时间；
- CPU reference 历史时间；
- available device memory；
- cold/warm context 状态。

### 决策

只有满足以下条件才 offload：

```text
predicted_gpu_convert
+ predicted_transfer
+ predicted_kernel
+ predicted_verify
< predicted_cpu
```

cost model 必须记录预测和实际误差。预测连续失准时自动提高阈值或停用对应 GPU Pass，不能为了保持 GPU 使用率而牺牲编译时间。

## Benchmark 与测量合同

### 基线

Phase 0 只测量当前 CPU 编译路径和现有 HIR。它不把尚未存在的 FlowIR、GPU cold/warm、resident 或 incremental 数据写入基线，也不把当前 translation unit 的串行编译误称为 CPU 多线程 analysis。

同一 analysis 的 CPU 单线程与多线程比较从 Phase 3 开始；GPU cold、warm、resident 和 incremental 比较从 Phase 4 的 backend 可用后开始。届时完整比较集合必须包括：

1. CPU 单线程 reference；
2. CPU 多线程 reference；
3. GPU cold context；
4. GPU warm context；
5. GPU resident FlowIR；
6. GPU incremental changed-functions；
7. 现有 LLVM/Clang 路径；
8. 在适用时比较 LLVM 自带分析/优化。

### Workload

建立可复现生成器，至少覆盖：

- 10,000 个短函数；
- 1,000 个中等函数；
- 少量 10,000+ 指令超长函数；
- 10 万、100 万、500 万级 FlowIR instruction；
- 宽浅 CFG；
- 深窄 CFG；
- 大量循环和 join；
- 高 alias、低 alias、UnknownExternal；
- 大量部分字节写入；
- `goto`、`try/catch/throw`、checked failure；
- 多 translation unit 和增量修改；
- 真实 HitSimple 示例、标准库和兼容层 fixture。

合成 workload 只用于控制变量，不能替代真实项目结论。

### 指标

- source/HIR -> FlowIR 时间；
- serialization 和 upload 时间；
- kernel 时间；
- convergence/verification 时间；
- CPU fallback 时间；
- end-to-end wall clock；
- cold/warm/incremental latency；
- host RAM 和 device memory 峰值；
- fixed-point iteration count；
- cost-model prediction error；
- CPU/GPU 结果一致性；
- 诊断一致性；
- 生成程序的运行性能和体积；
- 有条件时测量能耗。

### 公平性

- CPU 使用实际可用核心数并报告线程数；
- GPU 时间包含必要转换、传输和验证；
- cold 与 warm 结果分开，不混合平均；
- 每组至少多次运行，报告 median 和离散程度；
- 不只与单核 CPU 比较；
- 不只报告单个 Pass 最高 speedup；
- 不从合成 workload 外推真实桌面编译收益；
- 失败和减速样本必须保留在报告中。

## 阶段计划

### Phase 0：测量基础设施与真实基线

**目标**

建立能够回答“时间花在哪里、规模多大、何时值得 batch”的数据面。

**完成状态**

- `CompilationMetrics` 与 `--timing-json=<path>` 已覆盖当前 HIR/LLVM 路径；外部工具保持合并的 `clang_backend_link` bucket，默认 CLI 输出不变。
- `tools/run_cpu_baseline.py` 已建立单进程、单次 `hsc` 调用的真实 workload runner，并将 instrumentation 开销与基线分开记录。
- `tools/generate_synthetic_workloads.py` 已生成确定性的 Phase 0 源码 workload；其 CMake smoke coverage 覆盖重复生成、短函数、多 translation unit、checked failure 与 `UnknownExternal` 输入。

这些成果只描述当前 CPU/HIR 路径。它们不构成 CPU 多线程 analysis、FlowIR 规模、GPU cold/warm/resident/incremental 或真实项目收益结论。

**后续约束**

- `--flow-ir-stats` 已输出函数、block、instruction、edge、object 和 View 数量；Phase 3 runner 以同一输入建立实际 analysis 的单线程/多线程 CPU reference，并单独记录其结果。
- 基准报告继续区分单进程 CPU 基线与 instrumentation 开销，并记录执行环境；不得把当前合并的 `clang_backend_link` 拆成未经实现的 backend 或 link 指标。

**理由**

当前只有小样本粗测。没有当前 CPU 路径的阶段计时就无法判断 GPU 应加速哪个桶，也无法验证 Amdahl 上限。第一轮只建立事实基线，不预设 GPU 运行或 CPU 并行 analysis 已存在。

**验收**

- 同一输入能稳定输出版本化的阶段计时和现有 HIR 规模；默认编译、诊断和 stdout/stderr 保持不变；
- 多 translation unit、`--emit-llvm` 和 `--c-compat` 路径分别记录实际到达的阶段；外部工具阶段只记录 `clang_backend_link`；
- timing 开销有单独测量，默认关闭时不影响现有 CLI 输出；
- benchmark 可复跑，并先建立当前 CPU 单进程端到端基线；
- CPU 多线程 reference、GPU cold/warm、resident 和 incremental 结果在对应实现存在前不得写入 benchmark 结论；
- 不对 GPU 收益作提前结论。

### Phase 1：Beta.22 最小语义合同

**目标**

先明确通用 as-if、并发求值许可和 effect contract，再让分析依赖这些合同。

**完成状态**

- `Standard.md` 第 1、8、12、17、18、19、20 章已形成 Beta.22 Draft，`effects(...)` 语法、FFI 默认值、违反规则和 EBNF 已同步。
- `effects(...)` 已进入 lexer/parser/AST/sema，并以 HIR `EffectContract` 保存显式 flags、read/write range 和 `noalias` pair。
- `extern` 无 clause 时继续按 `unknown`；显式 clause 会校验 item、对象/范围、`noalias`、`throws`/`nothrow`、`pure`/`readonly` 和冲突组合。
- 定义函数、template method 与 `impl op` 使用相同前端合同；FlowIR v2 保存函数 contract 摘要，供跨函数 reference summary 使用。
- `SemaEffectVerification` 从 HIR 函数体、内建 allocation/free/I/O/throw、直接 read/write 与递归调用 summary 验证显式合同；可证明的 range、`pure`、`readonly`、`nothrow` 和 `noalias` 违反在所有模式下诊断。无法静态解析的 alias 不被误判为违反，交由 checked runtime 在可观测路径处理。
- `checked` 为显式非 `unknown` contract 建立嵌套执行上下文，检查 compiler-lowered dereference、pointer store、core memory call 的 declared range，入口 `noalias`，以及 allocation/free、formatted output/input、throw；`static-checked` 不生成这些 bridge。raw/FFI address、`cstr` traversal、file handle 和 opaque `extern` 的检测边界已由 `--target-info` 明示。
- 分层回归覆盖直接和跨函数违反、covered range、静态/动态 `noalias`、正常上下文退出与 `static-checked` 零插桩；`cmake --build build --parallel`、`./build/hsc_unit_tests`（393 tests）和 CTest（206/206）已通过。

**验收状态：已完成。**

**理由**

effect 和 noalias 信息如果只是编译器私有假设，会产生不可审计的优化风险；进入标准后才能作为跨函数分析和 FFI 优化依据。

**验收**

- 旧的无 contract 源码保持原语义；
- 未标注 `extern` 保守处理；
- 可证明的错误 contract 在所有模式诊断；
- checked/static-checked 行为符合新条款；
- EBNF、诊断清单和实现定义清单同步。

### Phase 2：CPU `FlowIR` 骨架

**目标**

建立与 GPU 无关、可验证、可 dump、可序列化的紧凑中端。

**完成状态**

- `include/hitsimple/flowir/` 与 `src/flowir/` 提供初始 schema v1、32-bit stable ID、扁平表、SoA/CSR 邻接表和无 pointer graph 的 device-ready records；Phase 3 的 effect contract metadata 将当前 schema 演进到 v2，数量溢出会显式报错。
- HIR -> FlowIR builder 覆盖当前 HIR 的表达式和语句种类。它保留最终模板绑定、object slice、View、function signature、multi-return、CFG、`goto`、exceptional edge、显式 lifetime action 和静态 `UnknownExternal` 边界。
- AST `SourceRange` 在 HIR 构造期保留，FlowIR source map 直接消费该 provenance，不重新解析源码；用户模板成员 View 按 `(object, offset, byteLength)` 恢复成员模板。
- verifier 检查 ID、模板/View 长度、object-slice 边界和溢出、SSA dominance、CFG CSR 对称、return View 数量、exceptional edge、lifetime action 与 source map。deterministic dump、binary serialization 和统计接口均已实现。
- `--dump-flow-ir` 先构建并验证再输出稳定 dump；`--flow-ir-stats` 输出核心表计数。两项均为显式 action，不进入默认编译或 LLVM lowering。
- `tests/flowir/` 覆盖确定性 dump/serialization、CFG/exception/lifetime/source map、坏 IR 拒绝和 24 组确定性生成输入的不变量；现有 HIR -> LLVM 路径未移除。

**理由**

GPU kernel 不能建立在未验证的数据模型上。FlowIR 本身必须先在 CPU 上成为可靠边界。

**验收 Gate A**

- 通过。`cmake --build build --parallel` 和 `ctest --test-dir build --output-on-failure --parallel 4` 于本轮通过，CTest 为 198/198。
- FlowIR 单元测试验证同一输入的 dump/serialization 稳定、source range、View、object slice、exception 和 cleanup；verifier 能拒绝典型坏 CFG/View。
- 额外以 `--dump-flow-ir` 成功覆盖 comprehensive、multi-return、user template `impl op`/method/format、dynamic input、static local pointer、nested/rethrow exception、C compatibility aggregate 等代表性输入。GPU、CPU reference analysis、默认编译路径和 LLVM lowering 仍未使用 FlowIR。

### Phase 3：CPU reference analysis

**目标**

在没有 GPU 变量的情况下定义正确算法和结果合同。

**完成状态**

- `include/hitsimple/flowir/Analysis.h` 提供 reachability、backward liveness、View range、object lifetime、initialized bytes 和 interprocedural effect summary。所有 CFG successor（包含 exceptional edge）参与 transfer；未知 slice 和未标注 extern 保持 `UnknownExternal`/`unknown`。
- effect summary 按 call graph monotone union 收敛，可处理递归 SCC；显式 extern contract 覆盖默认 unknown。lifetime 使用 `Dead`/`Live`/`MaybeLive` lattice，initialized bytes 在 join 取交集，View range 使用 `Known(object, interval)` 到 `Unknown` 的保守 lattice。
- `AnalysisCache` 通过 FlowIR stable serialization fingerprint 命中或失效；`--dump-flow-ir-analysis[=threads]` 输出稳定 CPU reference，指定线程数只并行独立函数的同一算法。
- `tools/run_flowir_analysis_baseline.py` 交替运行单线程和多线程 reference，以稳定 dump SHA-256 为差分 oracle；报告明确为 CPU-only、顺序 `hsc` 调用，不混入 GPU 数据。

**理由**

CPU reference 是 GPU 差分 oracle，也能直接改善 static-checked 能力。没有 reference 就无法区分算法错误、并发错误和 backend 错误。

**验收 Gate B**

- 通过。`cmake --build build --parallel` 与 `ctest --test-dir build --output-on-failure --parallel 4` 于本轮通过，CTest 为 200/200。
- FlowIR unit tests 覆盖循环、exception edge、UnknownExternal、initialized bytes、effect contract、cache invalidation，以及单线程/4-worker 序列化逐字节一致；CLI 与 baseline runner CTest 覆盖 `--dump-flow-ir-analysis=4` 和 single/parallel SHA-256 differential。
- 结果只在可证明的 object slice/interval 上写为 known；dynamic View、raw address、未知 extern、未知 alias 和无法闭合的生命周期都保持 conservative，不产生安全结论或修改默认编译路径。

### Phase 4：GPU backend 与首个 fixed-point Pass

**目标**

实现统一 backend、设备内存管理和第一批 GPU analysis。

**完成状态**

- 首个 backend 确定为 OpenCL 1.2 C API。构建不要求 OpenCL SDK、`clinfo` 或静态链接 OpenCL；Linux 运行时动态加载 `libOpenCL.so.1`（或 `libOpenCL.so`），因此不支持 OpenCL 的开发机和 CI 继续使用同一 CPU 构建。
- `include/hitsimple/gpu/GpuAnalysis.h` 定义 `auto`、`opencl`、`cpu` 模式，以及设备、显存预算、阶段计时、fallback 原因和验证状态。当前实现从 OpenCL platform 中选择第一个 GPU device，查询名称、OpenCL API version 和总全局显存。OpenCL 没有可移植的空闲显存查询，`available_memory_bytes` 是标记为估算的总全局显存；实际分配失败必须回退 CPU。
- GPU 输入从已验证的 FlowIR CFG、entry block、block-local use/def bitset 和 View range transfer 表打包而来。OpenCL 路径上传这些表，缓存同一 FlowIR fingerprint 的 device buffers，并以嵌入的 OpenCL C（`-cl-std=CL1.2`）执行 reachability、backward liveness，以及 `ReinterpretView`/`Convert`/`Unary`/`ByteSwap` 的已知 object interval 传播；CPU reference 仍负责完整结果和逐项 differential verification。
- `--dump-flow-ir-gpu-analysis[=auto|opencl|cpu]` 是唯一入口；`--flow-ir-gpu-report=<path>` 记录 preparation、upload、kernel、download、verification 与 CPU fallback 时间。该 action 不改变默认编译、诊断、HIR -> LLVM 或 CPU reference 路径。
- `tools/run_flowir_gpu_analysis_baseline.py` 交替运行 CPU 与 GPU action，以稳定 analysis dump SHA-256 作 oracle，并把 GPU report 原样写入结果。fallback 明确不是 GPU 性能测量。
- OpenCL ICD loader 不可用、无 GPU device、不支持 OpenCL 1.2 C、显存预算不足、device OOM、program-build/kernel failure、固定点超界、每轮同步后的 wall-clock budget 超时或 CPU/GPU facts 不一致时，都返回完整 CPU reference 结果；不保留未验证的 GPU facts。单个已卡死的驱动调用不能由同一线程抢占，仍依赖 driver/OS watchdog。

**本轮验证与限制**

- 本机 `cmake --build build --parallel`、`./build/hsc_unit_tests`（396 tests, 0 failures）以及 GPU 相关的 3 个 CTest 均通过；本机无 OpenCL GPU，`auto` 的 no-device CPU fallback 只作为 fallback 回归，不进入 GPU 性能结果。
- Colab `hitsimple-opencl-t4` 已实测为 Tesla T4（15 GiB，driver 580.82.07），NVIDIA ICD 的 GPU device 支持 OpenCL C 1.2。远端以 GCC + LLVM 19.1.7 重建后，`hsc_unit_tests`（396 tests）与 GPU filter（6 tests）通过；强制 `opencl` 的 CLI 和 4 次 CPU/GPU differential 均为 `executed_backend=opencl`、`fallback_reason=none`、`gpu_facts_verified=true`。其中 program-build 注入实际触发 OpenCL compiler error，OOM、kernel failure 与无设备分支均回退完整 CPU reference。
- 新增 `hsc_flowir_gpu_benchmark`，它在同一进程内测量 CPU multi-worker、fresh-context cold、warm non-resident 与同一 `GpuAnalyzer`/同一 FlowIR 的 resident reuse，并对每次 GPU 结果执行 CPU differential。T4 上的小 fixture 已确认 resident buffer reuse；2,000,602-instruction、101-function workload 的三次采样结果已归档到 `Google:ColabArchives/hitsimple-opencl-gate-c/hitsimple-opencl-t4-gate-c-20260716.tar.gz`。
- `.codex/skills/operate-colab-cli/scripts/setup-hitsimple-opencl-t4.sh` 已在同一 T4 session smoke：宿主机以 `rclone copyto` 将精简源码包写入 `Google:`，VM 用 rclone 直拉并校验 SHA-256 后完成 GCC + LLVM 19 构建；源码未经 `colab upload` 传输。

**理由**

liveness 易于验证，View range 能验证 HitSimple 特有价值。两者组合可以避免原型只证明一个与语言无关的 toy kernel。

**验收 Gate C**

- GPU 与 CPU reference 逐项一致；
- 重复运行结果和诊断稳定；
- kernel failure、OOM 和无设备均可靠 fallback；
- 100 万级 instruction 的 warm/resident GPU analysis 在包含验证后至少达到 CPU 多线程的 1.5 倍，或记录未达到并停止默认集成；
- 小 workload 不因 `auto` 模式启动 GPU 而出现明显回归。

**Gate 状态：已执行，性能门禁失败，GPU 路线终止。** 正确性、重复稳定性、无设备和受控 failure fallback 均通过；性能未通过。T4 标准实例的 CPU 为 2 logical workers：小 workload 的 `auto` median 为 350.868 ms，CPU multi-worker median 为 13.594 ms，出现约 25.8 倍回归；百万级 workload 的 resident median 为 9.811 s，CPU multi-worker median 为 2.926 s，resident speedup 仅为 0.298x，低于 1.5x 门槛。GPU 成功路径还包含完整 CPU reference verification，不能满足本计划的端到端 1.5x 收益合同。默认 GPU integration、`hscd`、FlowIR -> LLVM migration 与高成本 GPU optimization 不再推进，CPU reference 保持默认路径。

本分支作为 OpenCL GPU 研究归档保留，不再重新评估 Gate C。未来若有独立研究项目，应先建立不依赖每次完整 CPU reference 的正确性合同，并证明任务具有足够大的规则并行规模；不得把 loader 可用、CUDA 可用或 CPU fallback 计为 GPU 结果。

### 已归档的后续 GPU 草案

原 Phase 5 的 `hscd`/显存增量缓存、Phase 6 的 GPU static-checked、Phase 7 的 FlowIR -> LLVM、Phase 8 的高成本搜索和 Phase 9 的产品化均未开始。它们依赖 Gate C 的端到端收益，现随 GPU 路线一并归档，不纳入主线 backlog。

若未来重启与 GPU 无关的 FlowIR 或 CPU 增量编译工作，必须另立目标、性能基线、正确性 oracle 和验收，不得继承本研究路线的未达成 gate。

## 任务依赖关系

```text
Phase 0 instrumentation
  -> Phase 1 standard contracts
  -> Phase 2 FlowIR
  -> Phase 3 CPU reference
  -> Phase 4 GPU prototype
       -> Gate C failed: archive research and retain CPU path
```

Phase 1 和 Phase 2 可以部分并行设计，但 FlowIR 不得依赖尚未确定的 effect 语法。GPU 后续 Phase 已归档，不再构成依赖链。

## 风险与应对

| 风险 | 影响 | 应对 |
|---|---|---|
| 当前真实程序规模过小 | GPU 无法摊销固定成本 | 先做 instrumentation 和 batch workload；默认保持 CPU。 |
| HIR -> FlowIR 转换占比过高 | Pass 加速无法转化为端到端收益 | resident cache、增量 segment；测量转换时间，不隐藏。 |
| 显存放大 | 大项目无法处理 | SoA/CSR、analysis-specific pruning、预估 working set、分批和 fallback。 |
| warp divergence | GPU 利用率低 | opcode 分组、规模分桶、专用 kernel、长函数 CPU fallback。 |
| alias 过于保守 | static-checked 收益不足 | effect/noalias contract、对象来源和 region 研究；unknown 保持安全。 |
| 并发结果不确定 | 诊断或优化漂移 | 单调 lattice、稳定 ID、确定性 commit 和最终 verifier。 |
| daemon 增加部署复杂度 | 本地使用体验下降 | daemon 后置；CLI 始终可单进程 CPU 编译。 |
| GPU vendor 锁定 | 可移植性和维护成本高 | backend interface；标准不绑定 vendor；首个实现只作为工具链选择。 |
| 与 LLVM 重复优化 | 增加编译时间但无代码收益 | 只保留有测量价值的 FlowIR Pass；与 LLVM baseline 比较。 |
| 为 GPU 过度修改语言 | 核心语义失焦 | 标准只加入可验证合同；借用/region 扩展必须通过实验 gate。 |
| benchmark 高估 | 错误产品决策 | CPU 多线程、cold/warm、真实项目、完整传输和失败样本全部报告。 |
| GPU CI 不可用 | 回归无法持续发现 | CPU reference 和 schema tests 必须常规运行；GPU tests 分层并允许专用 runner。 |

## 停止条件与保留成果

出现以下情况之一时，不继续把 GPU 路径推向默认启用：

- 100 万级代表性 workload 的 warm/resident 路径仍不能稳定超过 CPU 多线程；
- device memory 或转换成本随规模增长不可控；
- 正确性需要依赖无法可靠验证的 race 行为；
- static-checked 精度没有实际提升；
- cost model 无法稳定避开减速 workload；
- daemon 的维护成本高于得到的收益。

即使停止 GPU 产品化，以下成果仍应保留：

- 阶段计时和 benchmark；
- Beta.22 的通用 as-if/effect 合同；
- FlowIR、verifier、serialization 和 source map；
- CPU reference analyses；
- 函数 effect summary 和增量 cache；
- 减少文本 `.ll` 往返的 LLVM 后端改进。

## 近期执行顺序

已完成：

1. `CompilationMetrics` 与 `--timing-json=<path>`，只记录当前 HIR/LLVM 路径和 `clang_backend_link`。
2. 可复跑的 CPU 单进程基线 runner，独立报告 instrumentation 开销。
3. 确定性的源码级合成程序生成器；FlowIR 表规模已由 `--flow-ir-stats` 采集，并已接入 Phase 3 的可复跑 analysis baseline。
4. Beta.22 Draft 的 as-if、并发求值和 effect contract 条款，以及 `effects(...)` 的 lexer/parser/AST/sema/HIR 前端、完整函数体/调用链验证、checked runtime instrumentation 与 extern contract 结构验证。
5. Phase 2 `FlowIR` schema、HIR builder、source map、CFG/exception/lifetime、verifier、deterministic dump/serialization/stats 和 Gate A；默认 HIR -> LLVM 路径未变。
6. Phase 3 CPU reference analyses、FlowIR v2 effect metadata、single/multi-worker differential、cache/invalidation 和 Gate B；默认 HIR -> LLVM 路径仍未变。
7. Phase 4 OpenCL 1.2 dynamic-loader prototype、OpenCL reachability/liveness/受限 View range kernels、显式 GPU action/report、resident buffer cache、全链路计时、CPU differential runner、failure injection 与 `hsc_flowir_gpu_benchmark` 已完成；Gate C 已在 Colab T4 执行，但因小 workload 回归和百万级 resident 仅 0.298x 而失败，GPU 路线归档，默认路径未变。

下一项：

1. 保持 CPU reference 为默认路径；不启动 Phase 5–9、默认 GPU integration、FlowIR -> LLVM lowering 或高成本 GPU optimization。
2. 用已有 `--timing-json` 与 baseline runner 对真实 workload 做 CPU 端到端 profile，定位 preprocess、parser、sema、FlowIR analysis、LLVM emission 与外部链接中的首要瓶颈。
3. 只对已测得的首要瓶颈提出 CPU 并行、缓存或增量编译改进，并以端到端编译时间和现有差分回归验收。
4. 使用 `.codex/skills/operate-colab-cli/scripts/setup-hitsimple-opencl-t4.sh` 时，源码归档必须由 host `rclone copyto` 直接写入 `Google:` 并在 VM 回读 SHA-256；FUSE 挂载目录只用于访问，不得假设新文件立即可见。

GPU 研究分支在 Gate C 结论后归档；主线性能工作不再以 GPU 作为前提。

## GPU 研究的原完成定义（未达到）

本研究不以“写出了 OpenCL/HIP kernel”为完成标准。以下条件原本共同构成 GPU-native 路线的可交付阶段；Gate C 未达到，故不作产品交付承诺：

- 标准合同明确且实现不依赖未声明假设；
- FlowIR 能完整表达当前语言语义并通过 verifier；
- CPU reference 与 GPU 结果完全一致；
- 失败时可靠 fallback；
- benchmark 包含转换、传输、验证和 CPU 多线程基线；
- 真实 workload 达到预先规定的收益 gate；
- 文档只承诺已验证的平台、规模和模式；
- 未解决的 alias、FFI、显存和长函数边界明确保留。

当前状态：Phase 0 的计时、单进程基线和确定性源码 workload 基础设施、Phase 1 的 Beta.22 effect contract 闭环、Phase 2 的 CPU `FlowIR` 骨架与 Gate A、Phase 3 CPU reference analysis 与 Gate B，以及 Phase 4 的 OpenCL backend、reachability/liveness/受限 View range、CPU differential、failure fallback 和 resident benchmark 已实现。Gate C 已在 Colab T4 上完成正确性和性能测量，但性能门禁失败，GPU 路线归档；高成本 GPU optimization、FlowIR -> LLVM lowering、默认编译路径改动与 Phase 5 均未开始。
