# C/C++/Rust 混合构建与 C ABI 互操作计划

## 当前状态

HitSimple 已有从源码到 LLVM IR、object、staticlib 与可执行程序链接的编译闭环。`hsc` 接受 HitSimple 源码或 `--c-compat` 的受限 C 输入，并可在 Linux x86_64/AArch64 可执行程序构建中显式接收原生 C/C++ 源码、object/archive/shared-library 链接输入及一个 Cargo `staticlib`。核心语言通过显式 `extern "C"` 提供稳定互操作合同。

本计划的目标是在 Linux x86_64 与 AArch64 上，让 HitSimple、C、C++、Rust 通过稳定 C ABI 组成同一程序并双向调用。它不涉及 CPU 架构交叉编译。

## 当前执行状态

- Phase 1 已完成：显式 `extern "C"`、标量 ABI 检查、object/staticlib 产物和 C ABI 降低已落地；staticlib 包含 HitSimple runtime。x86_64 本地已验证 C 调 HitSimple staticlib、HitSimple object 调 C object、`llvm-ar` 缺失和输出组合诊断。
- Phase 2 已完成：`--c-source`、`--cxx-source`、`--link-input`、`-L`、`-l`、`--link-arg`、`--entry`、`--clangxx` 与 `--linker-language` 已接入。x86_64 本地回归覆盖 HitSimple/native 两种入口、C/C++ 两个方向、archive、库搜索路径、用户链接参数及编译/链接工具诊断。
- Phase 3 已完成：`--cargo-manifest`、`--cargo-package`、`--cargo-profile`、`--cargo-features` 与 `--cargo-no-default-features` 已接入。实现以 Cargo JSON 定位唯一 `staticlib`，转发受支持的 native search path 与 `static`/`dylib` requirement，并明确拒绝虚拟 workspace 缺 package、非 staticlib、未解析的 native requirement 与 Cargo 失败。x86_64 本地已验证 HitSimple 调 Rust 和 Cargo 管理的 Rust 调 HitSimple staticlib。
- 本轮 x86_64 本地已执行 `cmake -S . -B build`、`cmake --build build --parallel` 和 `ctest --test-dir build --output-on-failure --parallel 4`；CTest 为 234/234 通过。
- AArch64 release workflow 已覆盖 object/staticlib、C/C++ mixed 与 Cargo 回归，但本轮尚未取得远端运行结果；Linux AArch64 验收仍待 CI 证据。

## 已确定的边界

- 唯一稳定边界是 C ABI。C++ 必须经 `extern "C"` wrapper；Rust 必须使用 `pub extern "C"`、无修饰导出和 `staticlib`。不承诺 C++ name mangling、C++ class ABI 或 Rust ABI。
- 首版允许��数、`bool`、`f32`、`f64`、`addr`、`cstr`、`handle` 与 `()`。结构体只能通过指针或 opaque handle 传递；拒绝 `f16`、`f128`、多返回值、模板/View、vararg 和结构体传值。
- C++ exception、Rust panic 和 HitSimple 异常不得跨越 C ABI 边界。调用方 wrapper 负责在本语言内处理异常或 panic。
- 首版正式支持 Linux x86_64/AArch64。Windows、macOS、共享库生成和跨目标编译暂不在范围内。
- 原有 `extern`、`--c-compat` 和其已支持的 C aggregate ABI 范围保持兼容；新的显式 C ABI 合同不扩大它们的承诺。

## Phase 1：显式 C ABI 与 HitSimple 库产物

状态：已完成本地验收；AArch64 仍待 CI 结果。

### 交付

- 在 `Standard.md`、lexer、parser、AST、HIR、sema 和 LLVM lowering 中增加两种语法：

  ```hs
  extern "C" native_add(value as i32) -> i32

  extern "C" func hsc_increment(value as i32) -> i32 {
      return value + 1
  }
  ```

  前者导入 C ABI 函数，后者导出无修饰 C 符号。语义分析在边界处验证类型、禁止不支持的控制流/异常穿越，并把 ABI 信息保留到 LLVM 函数声明和定义。
- 增加 `--crate-type=bin|object|staticlib`，并以 `--emit-object` 作为 `object` 的等价入口。`staticlib` 打包 HitSimple object 与 runtime 成员；C/C++/Rust 外部库保持为父链接步骤的依赖。
- 使用 LLVM `TargetMachine` 发射 object，并用受控的 `llvm-ar` 调用生成 archive。保留现有 `--emit-llvm` 行为与临时文件清理约束。

### 验收

- 单元测试覆盖 `extern "C"` 的解析、HIR ABI 标记、IR calling convention 和无修饰符号。
- C 调 HitSimple staticlib、HitSimple 调 C object 都可在 Linux x86_64/AArch64 运行。
- 无效 C ABI 类型、非法导出、缺失或重复入口、缺失 `llvm-ar` 和 object/staticlib 输出错误必须给出明确诊断。

## Phase 2：C/C++ 源码与外部链接输入

状态：已完成本地验收。位置参数仅表示 HitSimple 源码；C/C++ 源码单独编译为临时 object，再由选定的 C 或 C++ driver 完成一次父链接。`--entry=hsc` 要求恰有一个 HitSimple `main`，`--entry=native` 禁止 HitSimple `main` 并要求 C/C++ source 提供入口。

### 交付

- 增加 `--c-source <path>`、`--cxx-source <path>`、`--link-input <path>`、`-L <dir>`、`-l <name>` 和 `--link-arg <arg>`。位置参数继续只表示 HitSimple 源码，避免按扩展名隐式猜测语言。
- 增加 `--entry=hsc|native`。默认 `hsc`，且必须恰有一个 HitSimple `main`；`native` 禁止 HitSimple `main`，要求 C 或 C++ 输入提供入口。object 与 staticlib 不要求 `main`。
- 保留 `--clang`，新增 `--clangxx` 和 `--linker-language=c|cxx`。有 `--cxx-source` 时默认使用 C++ 链接驱动；只有 archive/object 输入时由 `--linker-language` 明确选择。
- C/C++ 源码先由 Clang/Clang++ 独立编译为临时 object，再与 HitSimple object、runtime 和用户链接输入按确定顺序完成一次最终链接。所有外部进程继续使用 argv 传参，不拼接 shell 命令。
- 更新 `USAGE.md` 与 README，分别给出 HitSimple 入口和 native C/C++ 入口的最小命令、C++ wrapper 要求、C ABI 类型表及非目标范围。

### 验收

- HitSimple `main` 调用 C 与 C++ wrapper；C/C++ `main` 调用 HitSimple staticlib；两种方向均通过运行回归。
- C++ archive-only 链接、`-L/-l`、用户 `--link-arg` 和 C++ 链接驱动选择均有 CLI 回归。
- 工具链不存在、编译失败、链接失败和错误链接语言必须保留原始工具诊断并附带 `hsc` 上下文。

## Phase 3：Cargo staticlib 集成

状态：已完成本地验收。Cargo 通过 `--message-format=json-render-diagnostics` 返回产物和诊断；实现只接受唯一的 `staticlib` archive，使用 JSON 中的 native 路径和 `static`/`dylib` link requirement，其他 requirement 明确失败。

### 交付

- 增加 `--cargo-manifest <Cargo.toml>`、可选 `--cargo-package <name>`，以及 profile/features 选择。虚拟 workspace 必须显式指定 package。
- `hsc` 调用 Cargo 构建声明为 `staticlib` 的库，解析结构化 Cargo 输出以取得确切 archive 和所需 native link requirements，再加入最终链接。扩展进程支持以保存并转发 Cargo 诊断。
- Rust `main` 不交给 `hsc` 直接链接：由 `hsc --crate-type=staticlib` 生成 C ABI 库，Cargo 通过 build script 或显式 native link 配置链接该库，并继续负责 Rust runtime、依赖、profile 和最终链接。
- 增加最小 Cargo fixture：一条路径让 HitSimple 调 Rust staticlib，另一条路径让 Cargo test crate 调 HitSimple staticlib。

### 验收

- Cargo staticlib 可被 HitSimple 可执行程序调用，并在 Linux x86_64/AArch64 运行。
- Cargo 管理的 Rust 程序可通过 C ABI 调用 HitSimple staticlib。
- 缺失 Cargo、manifest 无对应 package、目标不是 staticlib、Cargo 构建失败和未解析的 native link requirement 都明确失败，不回退到猜测产物路径。

## 验证与收口

- 每个 phase 先补 parser/sema/codegen 单元测试，再补 CLI 与真实运行回归；C++ 和 Rust fixture 必须分别证明两个调用方向。
- 完成后运行 `cmake --build build --parallel`、`ctest --test-dir build --output-on-failure --parallel 4`，并在 Linux x86_64/AArch64 CI 或等价 runner 中运行混合构建矩阵。
- 更新 `--target-info` 或等价实现信息，使其准确说明 C ABI 支持面、runtime 链接方式及不支持范围；不得把本地构建成功描述为跨平台支持。

## 下一步

取得 GitHub Actions Linux x86_64/AArch64 原生 runner 的混合构建矩阵结果。x86_64 应运行完整 CTest；AArch64 应运行 C ABI object/staticlib、C/C++ mixed 和 Cargo 回归。CI 未通过前，不把本机 x86_64 结果扩大为 AArch64 或跨平台支持结论。
