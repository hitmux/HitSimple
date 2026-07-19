# 标准库合同与多 Provider 实施计划

## 状态

Phase 0–3 已完成。本计划取代已结束的 C/C++/Rust 互操作计划，范围仅覆盖 `Standard.md` Chapter 14 的 HitSimple 标准库实现与验收。当前进入 Phase 4：跨平台结论仍须以 GitHub Actions 的实际 workflow 为准。

## 目标

让 Chapter 14 的每个标准库 API 都具备：

- 明确的官方 `.hsh` 入口和所属头；
- 唯一的、可机器读取的 View 语义合同；
- 明确的默认实现 provider；
- 覆盖 `unchecked`、`static-checked`、`checked` 的诊断与运行时行为；
- 已验证的产物链接和平台边界。

这里的“完整”指完整实现 HitSimple 标准库合同，不承诺导入完整 C 标准库，也不扩大 `--c-compat` 或 FFI ABI 范围。

## 当前基线

- `stdlib/StandardLibraryManifest.json` v1 覆盖全部 75 个 `BuiltinId`；每项记录公开性、Chapter 14 归属、View 参数/结果模式、具体 overload、provider、safety obligations 和官方声明。
- C++ `hsc_stdlib_manifest_tool` 使用 LLVM JSON API 生成 typed C++ registry 以及 7 个正式头：`stdlib.hsh`、`string.hsh`、`stdio.hsh`、`math.hsh`、`ctype.hsh`、`time.hsh`、`assert.hsh`。源码树不再保留手写 `.hsh` 声明；CMake 会拒绝重新加入的手写标准头。
- 预处理器将官方头记录为 `standardHeaders` 并使用语义标记；sema 再按头文件归属注册 builtin。普通手写 `extern` 不能替代标准函数导入。
- sema 从 generated registry 注册 builtin，HIR 标准调用保存 `BuiltinId`、provider、结果规则和 overload index；现有 codegen 行为保持不变。
- `stdlib/ctype.hs` 是首个可随用户编译单元注入的 `CoreHs` source module；生成和安装目录同时包含该 module 与 7 个正式 `.hsh`。驱动会按 manifest 收集模块依赖、每个链接单元去重，并在 executable、`--emit-llvm`、`--emit-object` 和 `--crate-type=staticlib` 路径编译或合并所需 module。
- 默认 `optimized` selection 按 manifest 选择实现（`ctype` 为 `Intrinsic`）；`--stdlib-provider=reference` 选择 manifest 声明的 reference 实现（`ctype` 为 `CoreHs`），用于可观察行为和产物差分验证。
- `printf`、`print`、`fprintf`、`scanf`、`fscanf` 已有专用 HIR/codegen/runtime 描述符协议；checked memory/string/I/O 已有 `hs_*` runtime bridge。

当前实现在功能上已经覆盖大量 Chapter 14 能力；本计划不将既有测试通过直接视为完整合同闭环。

## 稳定设计决策

### 1. 一个结构化合同真源

新增受版本控制的 JSON manifest `stdlib/StandardLibraryManifest.json`，作为标准函数合同的唯一真源。它由使用 LLVM JSON API 的 C++ build tool 读取，不引入 Python、未固定的脚本解释器或第三方 manifest 解析依赖。它必须包含：

- `BuiltinId`、函数名、所属 `StandardHeader`；
- `visibility` 与 `standardSection`，明确区分公开 Chapter 14 API 和 compiler-only builtin；
- 参数模式：`view`、`lview`、`mem_view`、`mem_lview`、`cstr_view`、`handle`、`iN/uN/fN/T`；
- 具体重载展开、返回模板和长度规则；
- `left-context`、动态 `none[len]`、格式参数等特殊结果规则；
- 静态诊断和 checked-mode runtime obligations；
- 默认 provider、参考 provider、所需 source module 或 runtime symbol。

所有 75 个现有 `BuiltinId` 必须进入 manifest，不能静默遗漏。公开 API 必须恰有一个 `StandardHeader`；盘点出的 compiler-only builtin 不得伪装成标准库入口，须标为 internal 并迁出公开标准库注册路径。

build tool 从 manifest 生成公开标准函数声明和说明、typed C++ registry 数据，并校验 provider 完整性。`.hsh` 中的 include guard、宏和类型别名等非函数内容可以保留为小型手写模板；公开标准函数声明不得手写。安装包只携带生成后的正式 `.hsh`。现有 `BuiltinSpec::signature` 字符串及 sema 中手工 `addBuiltin(...)` 列表不再承担合同真源职责。

普通 core `extern` 不能表达所有标准库 meta-signature，因此生成的 `.hsh` 是官方用户接口；真实的 View 合同以 manifest 的结构化数据为准，不能由近似的普通 `extern` 参数类型推导。

### 2. provider 只决定实现，不改变语义

每个 HIR 标准调用保存 `BuiltinId`、已解析的具体 overload、结果规则和已知 safety obligations。codegen 仅根据这些已固化信息选择 provider，不能按裸函数名重新判断语义。

每次编译在编译期选择一个 provider selection，不引入运行时分派。默认 `optimized` selection 按 manifest 固定每个 API 的实现；`--stdlib-provider=reference` 选择相应的 reference implementation，用于与默认优化 provider 的差分验证。

| Provider | 用途 |
| --- | --- |
| `Semantic` | `length`、`resize_bytes` 等必须参与 View/长度推导的能力。 |
| `Intrinsic` | 数值转换、`byte_swap`、整数和浮点基础运算等可直接生成 LLVM IR 的路径。 |
| `CoreHs` | 可用 HitSimple 自身实现的可移植参考实现。 |
| `RuntimeBridge` | checked object tracking、时间、断言、目标相关 `f128` 等需要宿主或 ABI 支持的能力。 |
| `LibcBridge` | 与宿主 C 库等价的文件、字符串、随机数和进程能力；checked 模式按合同改走 wrapper。 |
| `FormatProtocol` | `print`、`printf`、`fprintf`、`scanf`、`fscanf` 的描述符和 left-context 协议。 |

### 3. 标准库 source module

适合语言实现的库使用 `stdlib/<module>.hs`。当本次编译实际调用某个 `CoreHs` API 时，驱动将对应 module 作为内部虚拟翻译单元加入同一链接单元：

- 同一个 module 在一次编译中至多加入一次，并按 manifest 声明的依赖顺序处理；
- 实现符号使用保留内部名称，用户仍只调用公开标准函数名，且不能重定义这些名称；
- `--emit-llvm`、`--emit-object` 和 `--crate-type=staticlib` 都必须包含所需 module，不能留下未定义符号；
- module 不得借助 `--c-compat` 或未声明的 host ABI；需要 host 能力时仍经正式 builtin/runtime bridge；
- 可对内部 module 使用以 compiler version、target、safety mode、provider 和 source hash 为键的缓存，但缓存不是正确性的前提。

`CoreHs` 是参考实现来源，不要求默认产物执行慢路径。优化 provider 必须通过同一合同和差分测试证明等价。

### 4. 固定的 provider 边界

| 头 | 默认 provider 规划 |
| --- | --- |
| `stdlib.hsh` | `length`、`resize_bytes` 为 `Semantic`；转换和 `byte_swap` 为 `Intrinsic`；分配在 checked 模式走 runtime、其他模式走 libc；随机数与进程控制为 host bridge。 |
| `string.hsh` | 默认 libc；checked 模式走 `hs_mem*`/`hs_str*`，负责边界、NUL、重叠和动态对象状态。 |
| `stdio.hsh` | 普通文件 I/O 为 libc 或 checked wrapper；格式化和扫描始终使用 `FormatProtocol`。 |
| `math.hsh` | `f32/f64` 使用 LLVM intrinsic 或 libm；`f16` 经过 `f32`；`f128` 使用目标 runtime。 |
| `ctype.hsh` | 默认 `Intrinsic`；`ctype.hs` 提供 `CoreHs` ASCII reference 实现。 |
| `time.hsh` | `hs_time_ms`、`hs_clock_ms` runtime bridge。 |
| `assert.hsh` | `hs_assert`、`hs_panic` runtime bridge。 |

`printf`/`scanf` 协议、动态 View 长度、运行时格式检查、checked memory bridge 和 `f128` 不迁移为普通 `.hs` 函数。

## 分阶段实施

### Phase 0：合同真源与无行为变化迁移

**状态：已完成。** `StandardLibraryManifest.json` 是唯一的标准函数合同真源。LLVM JSON build tool 生成 registry 和正式 `.hsh`，CMake、static assertion 与 completeness tests 共同阻止缺失、未知、重复 `BuiltinId`、无 provider、无对应 header 的公开 API，以及手写标准头声明。手写 `BuiltinSpec::signature`、`BuiltinLowering` 和 sema 的 75 项 `addBuiltin(...)` 清单均已移除。

**验收证据：** `cmake -S . -B build-stdlib-phase0 -DBUILD_TESTING=ON`、`cmake --build build-stdlib-phase0 --parallel 4` 与 `ctest --test-dir build-stdlib-phase0 --output-on-failure --parallel 4` 已完成，234/234 通过。manifest 的缺失、重复和未知 `BuiltinId` 都在 generated-registry build 阶段被拒绝；安装到临时前缀后，安装版 `hsc --emit-llvm examples/hello.hs` 成功使用 7 个生成头。`StandardLibraryManifest_GeneratedHeadersMatchPublicRegistry` 同时核对每个生成头的 `extern` 声明与 public registry 恰好一致；`Sema_LowersFloatMathHelpers` 核对 HIR 中的 `Intrinsic` provider 和 `f64` overload identity。

### Phase 1：内部 `.hs` 标准库 module 基础设施

**状态：已完成。** 驱动按 manifest 收集 `CoreHs` module 及依赖，以内部翻译单元分析并合并 LLVM IR；object 与 static library 路径分别生成 module object。内部实现符号受保留名称检查保护；同一 module 在多用户翻译单元和 archive 中只加入一次。`--stdlib-provider=optimized|reference` 在编译期固定 provider，默认值为 `optimized`。

**验收证据：** `hsc_ctype_reference_emit_llvm_injects_module`、`hsc_ctype_optimized_emit_llvm_omits_module`、`hsc_ctype_reference_object_includes_module`、`hsc_ctype_reference_staticlib_includes_module_once` 与 `hsc_ctype_reference_multifile_deduplicates_module` 已通过；安装前缀回归 `hsc_installed_ctype_reference_all_artifacts` 覆盖 executable、LLVM IR、object 和 staticlib。

### Phase 2：`ctype` 垂直切片

**状态：已完成。** `stdlib/ctype.hs` 实现 `is_digit`、`is_alpha`、`is_alnum`、`is_space`、`to_upper` 和 `to_lower` 的 ASCII 合同。默认 `Intrinsic` 路径与 `CoreHs` reference 路径共享公开 `<ctype.hsh>` 和 manifest signature；`to_upper/to_lower` 的结果模板在 HIR 中保留为 `u8`，因此 `0x80` 按无符号值 `128` 比较，不会发生 `i8` 符号扩展。

**验收证据：** `ctype_reference.hs` 覆盖 `0x00`、ASCII 空白和字母/数字边界、`0x7f`、`0x80` 及大小写转换。`hsc_ctype_provider_modes_match` 对 optimized/reference 的 `--unchecked`、`--static-checked`、`--checked` 运行回归；reference timing JSON、IR、object、staticlib、安装前缀和多翻译单元回归均已通过。`ctest --test-dir build-stdlib-phase1 --output-on-failure --parallel 4` 于本轮通过 245/245；这是本机 Linux 证据，不替代其他平台验收。

### Phase 3：provider 合同闭环

**状态：已完成（本机 Linux 验收）。** 75 个 Chapter 14 API 均由 manifest 提供参数模式、具体 overload、结果规则、provider 与测试归属；生成 registry 会拒绝缺失或无效合同。`cstr_view`、可写 `lview`/`mem_lview`、`addr` 和 `handle` 的 sema 合同已对齐，未标注的 `addr` 返回值保持 `addr` 模板。`mem_*` 的直接 View 与 `addr`/`&x` 地址算术按不同语义 lowering，checked runtime 能检测动态越界与 `memcpy` 重叠。

格式化/扫描仍走 `FormatProtocol`：`FormatArgKind` 从 sema 保留到 HIR 和 runtime descriptor，动态 `%f` checked 回归覆盖该链路。`abs(iN_min)`、`calloc`/`fread`/`fwrite` 乘法溢出、零长度 memory operand、cstr 终止、文件 handle、字符串容量和动态 scan target 都有 static 或 checked 回归。`static-checked` 对可证明问题停止编译且不插入 runtime check。`--target-info`、README 和 USAGE 已说明 provider 的编译期选择、runtime 覆盖与平台边界。

**验收证据：** `cmake --build build-stdlib-phase1 --parallel 4`、`./build-stdlib-phase1/hsc_unit_tests`（409/409 通过）和 `ctest --test-dir build-stdlib-phase1 --output-on-failure --parallel 4`（268/268 通过）。其中 `hsc_checked_printf_dynamic_float` 覆盖动态 format descriptor，`hsc_checked_memcpy_bounds_runtime_error` 与 `hsc_checked_memcpy_overlap_runtime_error` 覆盖动态 checked memory，`hsc_target_info_documents_checked_and_calloc_status` 核对对外说明。本机结果不替代 Phase 4 跨平台证据。

### Phase 4：跨平台与发布验收

**目的：** 把标准库从本机构建事实提升为支持平台的实际证据。

1. 在 GitHub Actions 上运行每个支持平台的标准库 contract matrix。
2. 对 Linux native `f128`、Windows/macOS software `f128` 分别执行数学、转换、格式化和扫描回归。
3. 验证安装包和可移动包能发现生成的 `.hsh`、必要的 `.hs` source module、runtime archive 与 provider metadata。
4. 仅在真实 workflow 通过的平台更新支持声明。

**验收：**

- 每个发布包都可在安装/解压后的路径完成标准库 smoke；
- GitHub Actions 对相应平台实际通过；
- 不以本机交叉编译、模拟器或单平台结果声明其他平台标准库通过。

## 测试策略

测试从 manifest 自动或半自动生成 matrix，每个 API 至少覆盖：

1. 所属头导入、漏头、错头、禁止手写 `extern`；
2. sema 的参数 View 合同、具体 overload、返回模板和长度；
3. HIR 的 `BuiltinId` 与 provider identity；
4. LLVM IR 的 intrinsic/runtime/libc/source-module 选择；
5. `unchecked`、`static-checked`、`checked` 的可观察行为；
6. 需要链接的 API 在 executable、object、staticlib 和多翻译单元中的产物完整性；
7. target-dependent `f16/f128` 与 host ABI 路径。

reference 与 optimized provider 的差分测试必须比较输出、返回值、诊断、写入字节序列和 checked-mode failure，不只比较“能否编译”。

## 风险与暂缓事项

- 后续 `CoreHs` module 仍会影响多翻译单元、object 与 archive 产物，新增 module 必须复用已验证的去重、依赖排序和安装前缀回归。
- `mem_*`、字符串、文件、格式化、扫描和 `f128` 的行为跨 sema、LLVM、runtime；不得只改 registry 或头文件。
- checked runtime 目前无法完整追踪 `extern` global、FFI 裸地址和 file handle；这些边界必须继续在文档和测试中明确。
- 不为 source library 机制增加隐式 C 头、C vararg、未验证 ABI 或运行时动态 dispatch。
- 任何新的标准函数或扩展应先进入 `Standard.md`；本计划不以实现便利反推语言规范。

## 立即下一步

进入 Phase 4。先审计 GitHub Actions workflow 是否对 Linux、macOS 和 Windows 的标准库 contract matrix 运行 Phase 3 回归及安装/可移动包 smoke；再以对应 workflow 的实际成功结果验收 Linux native `f128` 和 Windows/macOS software `f128` 的数学、转换、格式化与扫描。未取得真实 CI 证据的平台不得更新支持声明。
