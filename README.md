# HitSimple

HitSimple 是一个实验性编程语言和 C++20 编译器。它的核心模型是“数据存于内存，含义来自显式解释”：同一段字节可以按整数、浮点、字符串、地址或结构布局解释，存储与解释模板保持分离。

编译流程已经形成可运行闭环：

```text
source -> preprocessor -> lexer -> parser -> AST -> sema -> HIR -> LLVM IR -> executable
```

`Standard.md` 是语言语义的依据。当前测试用于验证已有实现，不能替代对标准条款的逐项核对。

## 当前状态

`v0.2.0` 是 Beta prerelease，发布构建覆盖：

- Linux x86_64/aarch64 tar.gz。
- Linux amd64/arm64 DEB。
- macOS arm64/x86_64 tar.gz。
- Fedora 44 与 EL9 x86_64/aarch64 RPM。
- Windows x64 full/slim ZIP。
- VS Code 扩展 VSIX。

Windows 只生成当前 Windows x64 平台程序，目标 ABI 固定为 `x86_64-w64-windows-gnu`。macOS 使用当前 Darwin native target。发布范围不包含其他 UNIX，也不提供跨目标 `hsc --target`。

Windows 和 macOS 的 `f128` 通过 Boost 1.85.0 `cpp_bin_float` 的 113-bit 软件后端实现，在 runtime 边界按原始 IEEE 754 binary128 位模式传递。十进制转换和 binary128 编码显式执行 `roundTiesToEven`；NaN payload 的保留与传播仍属于实现定义行为。Linux 继续使用原生 LLVM `fp128` 与 glibc 接口。

## 从源码构建

需要：

- CMake 3.24 或更高版本。
- C++20 编译器。
- LLVM development packages。
- Bison。
- re2c。
- Clang。

macOS 构建还需要 Boost 1.85.0 或更高版本；发布包本身仍要求外部 Clang 18 或更高版本完成最终链接。

```bash
cmake -S . -B build
cmake --build build --parallel
```

生成的编译器位于 `build/hsc`。

## 第一个程序

仓库中的 `examples/hello.hs`：

```hs
$include <stdio.hsh>

func main() {
    new x[1]
    x %d= 42
    printf("%d\\n", x)
    return 0
}
```

```bash
./build/hsc examples/hello.hs -o hello
./hello
```

## Linux 发布包

DEB 安装布局：

```text
/usr/bin/hsc
/usr/libexec/hitsimple/hsc_mcpp
/usr/lib/hitsimple/libhitsimple_runtime.a
/usr/share/hitsimple/stdlib
/usr/share/doc/HitSimple
```

安装本地 DEB：

```bash
sudo apt install ./hitsimple_0.2.0_amd64.deb
```

arm64 使用 `hitsimple_0.2.0_arm64.deb`。软件包依赖 `clang-18`，或者 Debian 包版本不低于 18 的 `clang`。Ubuntu 22.04 和 Debian 12 默认仓库缺少 Clang 18 时，需要先按 [apt.llvm.org](https://apt.llvm.org/) 配置对应的 `jammy` 或 `bookworm` 软件源；DEB 不会修改系统软件源。

tar.gz 是可重定位目录包，解压后直接调用其中的 `bin/hsc`。编译器会相对自身位置寻找预处理器、标准库和 runtime。

Fedora 44 和 EL9 分别提供 x86_64/aarch64 RPM，例如：

```bash
sudo dnf install ./hitsimple-0.2.0-1.fc44.x86_64.rpm
```

EL9 使用 `1.el9` release suffix。RPM 安装到 `/usr/bin`、`/usr/libexec/hitsimple`、发行版库目录（x86_64/aarch64 为 `/usr/lib64/hitsimple`）和 `/usr/share/hitsimple`；系统需已有 `clang >= 18`，不内置 Clang，也不提供 GPG 签名。Fedora 44 和 EL9 是当前唯一承诺的 RPM 基线。

## macOS 发布包

```text
hitsimple-0.2.0-macos-arm64.tar.gz
hitsimple-0.2.0-macos-x86_64.tar.gz
```

解压后直接使用其中的 `bin/hsc`。包会相对可执行文件定位 `hsc_mcpp`、静态 runtime 和标准库，移动解压目录后仍可使用。系统需提供 Clang 18 或更高版本，可通过 `--clang`、`HITSIMPLE_CLANG` 或 PATH 选择。

macOS 只提供未签名的可重定位 tar.gz；不提供签名、notarization 或 PKG 安装器。

## Windows 发布包

完整包：

```text
hitsimple-0.2.0-windows-x86_64-full.zip
```

其中包含 `hsc.exe`、标准库、静态 runtime，以及固定版本 `llvm-mingw-20240619-ucrt-x86_64`。解压后无需安装 Visual Studio 或系统 Clang。

精简包：

```text
hitsimple-0.2.0-windows-x86_64-slim.zip
```

精简包要求用户提供兼容的 llvm-mingw/Clang 18。工具链解析顺序为：

1. `--clang <path>`。
2. `HITSIMPLE_CLANG`。
3. 完整包内的 `toolchain/bin/clang++.exe`。
4. `clang-18`。
5. PATH 中的 `clang`、`clang++`。

Windows 未指定 `-o` 时默认生成 `a.exe`。生成的用户程序静态链接 GCC/C++ runtime，只应依赖 Windows 系统 DLL。

## 编译器用法

```text
hsc [options] <input>...
```

常用命令：

```bash
hsc examples/hello.hs -o hello
hsc --checked examples/hello.hs -o hello-checked
hsc --emit-llvm examples/hello.hs -o hello.ll
hsc --clang /path/to/clang++ examples/hello.hs -o hello
hsc --target-info
```

完整 CLI 说明见 [`USAGE.md`](USAGE.md)。语言模型、标准模板、安全语义和 ABI 合同见 [`Standard.md`](Standard.md)。

## VS Code 扩展

扩展源码位于 `vscode/hitsimple/`，提供语法高亮、缩进、snippets、`$hsc` Problem Matcher，以及 Build/Run 命令。

```bash
cd vscode/hitsimple
npm ci
npm test
npm run package:vsix
code --install-extension dist/hitsimple-vscode-0.2.0.vsix --force
```

主要设置：

| Setting | Default | 作用 |
| --- | --- | --- |
| `hitsimple.compilerPath` | `hsc` | 编译器可执行文件名或路径。 |
| `hitsimple.mode` | `unchecked` | 选择 `unchecked`、`static-checked` 或 `checked`。 |
| `hitsimple.outputDirectory` | `.hitsimple/build` | 工作区内的输出目录。 |
| `hitsimple.additionalArgs` | `[]` | 按独立 argv 项传递的附加参数。 |

`.hs` 也被 Haskell 使用。如同时安装两种语言扩展，可在工作区中明确关联：

```json
{
  "files.associations": {
    "*.hs": "hitsimple"
  }
}
```

## 验证

```bash
./build/hsc_unit_tests
ctest --test-dir build --output-on-failure --parallel 4
```

VS Code Extension Host 测试在无图形界面的 Linux 环境中需要 `xvfb-run`：

```bash
cd vscode/hitsimple
xvfb-run -a npm run test:extension
```

## 已知边界

- checked 模式尚未完整追踪 `extern` global、FFI 裸地址和文件句柄边界。
- C compatibility 是受限子集。C struct 传值 ABI 仅支持已覆盖的 x86_64 SysV ELF 布局；Windows 和 Darwin 会明确拒绝。
- `f16` 数学使用 `f32` 计算后回写。Linux `f128` 依赖平台的 binary128/glibc 支持；Windows 和 macOS 使用软件 backend。
- `mut self` 和普通 `mut` 参数仍为保留语义并产生明确诊断。

## 许可证与第三方组件

HitSimple 项目自身目前为 `UNLICENSED`，`v0.2.0` 不改变这一状态。发布包包含所使用第三方组件的许可证或版权说明，包括 mcpp、LLVM/llvm-mingw 和 Boost Software License 1.0。使用或再分发前应分别核对这些条款。
