# C/C++/Rust 混合构建与 C ABI 互操作计划

## 状态

已完成。HitSimple、C、C++ 与 Rust 已可在 Linux x86_64/AArch64 上通过稳定 C ABI 组成同一程序并双向调用。实现与验收均不使用交叉编译、QEMU 或本机架构模拟。

## 已完成范围

### Phase 1：显式 C ABI 与 HitSimple 库产物

- `extern "C"` 导入和导出已贯通 lexer、parser、AST、HIR、sema 与 LLVM lowering；导出符号无修饰。
- C ABI 仅接受整数、`bool`、`f32`、`f64`、`addr`、`cstr`、`handle` 和 `()`；结构体传值、模板/View、`f16`、`f128`、多返回值和 vararg 均在编译期拒绝。
- `--crate-type=bin|object|staticlib` 与 `--emit-object` 已支持；`staticlib` 包含 HitSimple runtime，并使用受控的 `llvm-ar` 调用生成 archive。
- Linux x86_64/AArch64 已实际验证 C 调 HitSimple staticlib、HitSimple object 调 C object，以及输出组合、缺失 `llvm-ar` 和非法 C ABI 诊断。

### Phase 2：C/C++ 源码与外部链接输入

- `--c-source`、`--cxx-source`、`--link-input`、`-L`、`-l`、`--link-arg`、`--entry`、`--clangxx` 和 `--linker-language` 已实现。
- HitSimple/native 两种入口、C/C++ 双向调用、C++ archive、库搜索路径和用户链接参数均有运行回归；外部工具的编译与链接诊断保留原始上下文。
- 测试按平台解析 `llvm-ar`；Darwin 使用等价 linker argument，Linux 专属的错误 C++ linker-language 断言只在 Linux 运行。

### Phase 3：Cargo staticlib 集成

- `--cargo-manifest`、`--cargo-package`、`--cargo-profile`、`--cargo-features` 与 `--cargo-no-default-features` 已实现。
- Cargo JSON 用于定位唯一 `staticlib` 并转发受支持的 native search path 与 `static`/`dylib` requirement；虚拟 workspace 缺 package、非 staticlib、未解析 native requirement 与 Cargo 失败均明确诊断。
- 已验证 HitSimple 调 Rust staticlib，以及 Cargo 管理的 Rust crate 通过 C ABI 调 HitSimple staticlib。

## 稳定边界

- 唯一稳定边界是 C ABI。C++ 必须经 `extern "C"` wrapper；Rust 必须使用 `pub extern "C"`、无修饰导出和 `staticlib`。不承诺 C++ name mangling、C++ class ABI 或 Rust ABI。
- C++ exception、Rust panic 和 HitSimple `throw`/`try` 不得跨越 C ABI 边界。
- 正式支持范围是 Linux x86_64/AArch64。Windows、macOS、共享库生成和跨目标编译不在本计划范围；原有 `extern` 与 `--c-compat` 的 ABI 合同保持不变。

## 验收证据

- 本地 LLVM 18：`cmake -S . -B build-ci-make -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm`、`cmake --build build-ci-make --parallel 4` 和 `ctest --test-dir build-ci-make --output-on-failure --parallel 4` 均完成；234 项无失败。受限沙箱中的 GDB 用例明确标记为 `Skipped`，在获 `ptrace` 权限后实际通过。
- GitHub PR [#4](https://github.com/hitmux/HitSimple/pull/4) 的 [Release run 29583950246](https://github.com/hitmux/HitSimple/actions/runs/29583950246) 在提交 `e55dca6` 上验证：
  - [Linux x86_64](https://github.com/hitmux/HitSimple/actions/runs/29583950246/job/87896143507) 完整 CTest 234 项无失败，`hsc_debug_info_gdb` 实际通过。
  - [Linux AArch64](https://github.com/hitmux/HitSimple/actions/runs/29583950246/job/87896143305) 的 object/staticlib、C/C++ mixed、Cargo 与 GDB 定向回归 32/32 通过。
- `--target-info` 与 CLI 回归已说明 C ABI 支持面、runtime 链接方式和不支持范围；本地结果未被用作 AArch64 支持声明。

## 下一步

本计划没有剩余的实现或验收任务。PR #4 等待常规代码审查与合并；合并、tag 与 release 不属于本计划的验收步骤。
