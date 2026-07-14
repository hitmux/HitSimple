# 使用 `hsc`

`hsc` 会依次执行预处理、解析、语义分析、HIR lowering、LLVM IR 生成和最终链接。`Standard.md` 仍是语言语法与语义的依据；本文只描述编译器接口和发布包用法。

## 命令形式

```text
hsc [options] <input>...
```

除 `--help`、`--version` 和 `--target-info` 外，命令必须提供输入文件。未知选项、缺少 `-o` 参数值或输出目录不存在都会返回错误。

## 生成可执行文件

```bash
hsc examples/hello.hs -o hello
./hello
```

未指定 `-o` 时，Linux 和 macOS 默认生成 `a.out`，Windows 默认生成 `a.exe`。

输出目录必须预先存在：

```bash
mkdir -p out
hsc examples/hello.hs -o out/hello
```

### 多翻译单元

多个输入文件会独立预处理、解析和生成 LLVM module，随后统一链接：

```bash
hsc app/main.hs app/math.hs app/io.hs -o app/program
```

跨翻译单元的 `extern` 声明必须匹配。宏、typedef 和文件作用域 `static` 保持翻译单元局部。

## 选择安全模式

| Option | 行为 |
| --- | --- |
| `--unchecked` | 不插入安全检查，默认模式。 |
| `--static-checked` | 报告可静态证明的问题，不插入 runtime 检查。 |
| `--checked` | 执行静态检查，并为已支持的动态错误插入 runtime 检查。 |

```bash
hsc --checked examples/hello.hs -o hello-checked
hsc --static-checked examples/hello.hs -o hello-static
```

实际覆盖范围可通过 `--target-info` 查看，规范合同以 `Standard.md` 为准。

## 选择 Clang 工具链

最终生成可执行文件时，`hsc` 按以下顺序寻找 Clang：

1. 命令行 `--clang <path>`。
2. 环境变量 `HITSIMPLE_CLANG`。
3. Windows 完整包内的 `toolchain/bin/clang++.exe`。
4. `clang-18`。
5. PATH 中的 `clang`、`clang++`。

显式指定：

```bash
hsc --clang /opt/llvm/bin/clang++ examples/hello.hs -o hello
```

环境变量：

```bash
export HITSIMPLE_CLANG=/opt/llvm/bin/clang++
hsc examples/hello.hs -o hello
```

Windows PowerShell：

```powershell
$env:HITSIMPLE_CLANG = 'C:\llvm-mingw\bin\clang++.exe'
.\bin\hsc.exe examples\hello.hs -o hello.exe
```

找不到兼容工具链时，`hsc` 会在链接前返回明确错误。`--emit-llvm` 不执行链接，因此不要求 Clang 存在，即使传入的 `--clang` 路径无效也不受影响。

`HITSIMPLE_RUNTIME_SOURCE` 仍可在开发调试时覆盖静态 runtime。正常安装和发布包默认链接 `lib/hitsimple/libhitsimple_runtime.a`。

## 检查编译器和目标

```bash
hsc --version
hsc --help
hsc --target-info
```

`--target-info` 输出包括：

- 实际 target triple。
- Clang 路径和解析来源。
- runtime 类型与路径。
- `f128` backend。
- ABI、安全模式和标准库实现边界。

Windows target triple 固定为 `x86_64-w64-windows-gnu`；macOS 使用当前 Darwin native target。本版本只生成当前平台程序，不提供任意 `--target` 交叉编译接口。

## 检查中间表示

以下 action 每次只接受一个输入文件，且不能彼此组合：

```bash
hsc --dump-tokens examples/hello.hs
hsc --dump-ast examples/hello.hs
hsc --dump-hir examples/hello.hs
```

这些输出写到标准输出，不支持 `-o`。

## 生成 LLVM IR

```bash
hsc --emit-llvm examples/hello.hs
hsc --emit-llvm examples/hello.hs -o hello.ll
hsc --checked --emit-llvm examples/hello.hs -o hello-checked.ll
```

`--emit-llvm` 每次只接受一个输入文件。

## 仅预处理

```bash
hsc --preprocess-only examples/hello.hs
hsc -E examples/hello.hs -o hello.preprocessed.hs
```

该模式适合排查 `$include`、宏展开和条件预处理。它不进入 parser，因此 `--c-compat` 不改变预处理结果。

## C compatibility

```bash
hsc --c-compat path/to/program.c -o program
hsc --c-compat app/main.c app/support.c -o app/program
```

`--c-compat` 可以与 `--dump-ast`、`--dump-hir` 和 `--emit-llvm` 一起使用，不能与 `--dump-tokens` 或 `--target-info` 一起使用。

该模式是受限 C syntax 到 core AST 的转换层。多维数组、复杂 declarator、函数指针、指向数组的指针、C vararg 和 GNU 扩展会明确拒绝。C struct 传值 ABI 仅覆盖已支持的 x86_64 SysV ELF 布局；Windows 和 Darwin 不支持这一 ABI 范围。

## Action 约束

| Action | 输入数量 | 支持 `-o` | 支持 `--c-compat` |
| --- | --- | --- | --- |
| 默认 executable build | 一个或多个 | 是 | 是 |
| `--dump-tokens` | 一个 | 否 | 否 |
| `--dump-ast` | 一个 | 否 | 是 |
| `--dump-hir` | 一个 | 否 | 是 |
| `--emit-llvm` | 一个 | 是 | 是 |
| `--preprocess-only` / `-E` | 一个 | 是 | 无影响 |
| `--target-info` | 无 | 否 | 否 |

## Linux DEB

```bash
sudo apt install ./hitsimple_0.2.0_amd64.deb
```

arm64 使用：

```bash
sudo apt install ./hitsimple_0.2.0_arm64.deb
```

Ubuntu 22.04 与 Debian 12 需要先安装 Clang 18。以下命令按系统代号配置 apt.llvm.org；执行前应核对 [apt.llvm.org](https://apt.llvm.org/) 当前说明和签名方式。

Ubuntu 22.04：

```bash
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | \
  sudo tee /etc/apt/keyrings/apt.llvm.org.asc >/dev/null
echo 'deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main' | \
  sudo tee /etc/apt/sources.list.d/llvm-18.list
sudo apt update
sudo apt install clang-18
```

Debian 12 将上述两处 `jammy` 替换为 `bookworm`。DEB 本身不会添加或修改软件源。

安装后可验证：

```bash
hsc --version
hsc --target-info
hsc examples/hello.hs -o /tmp/hitsimple-hello
/tmp/hitsimple-hello
```

## Fedora 44 / EL9 RPM

RPM 只为 Fedora 44 与 EL9 提供 x86_64/aarch64 基线。两个发行版分别使用不同的 release suffix，避免对跨发行版 glibc 基线作出承诺：

```bash
sudo dnf install ./hitsimple-0.2.0-1.fc44.x86_64.rpm
sudo dnf install ./hitsimple-0.2.0-1.el9.aarch64.rpm
```

包依赖 `clang >= 18`，不内置 Clang，也不添加软件源或 GPG 签名。安装后可用与 DEB 相同的 `hsc --version`、`hsc --target-info` 和 hello 程序检查安装结果。

## macOS tar.gz

选择与机器一致的包：

```bash
tar -xzf hitsimple-0.2.0-macos-arm64.tar.gz
cd hitsimple-0.2.0-macos-arm64
bin/hsc --clang /opt/homebrew/opt/llvm@18/bin/clang path/to/hello.hs -o hello
./hello
```

Intel Mac 使用 `hitsimple-0.2.0-macos-x86_64.tar.gz`。解压目录可以移动；`hsc` 会相对自身位置发现预处理器、标准库和静态 runtime。macOS 需要外部 Clang 18 或更高版本；包未签名、未 notarize，且不提供 PKG。

macOS `f128` 与 Windows 一样使用软件 binary128 backend，覆盖字面量、算术、比较、数值转换、格式化、扫描和 `math.hsh` 的浮点入口。Linux 继续使用原生 `fp128`/glibc backend。

## Windows full/slim ZIP

full 包解压后可直接运行：

```powershell
Expand-Archive .\hitsimple-0.2.0-windows-x86_64-full.zip
cd .\hitsimple-0.2.0-windows-x86_64-full
.\bin\hsc.exe path\to\hello.hs -o hello.exe
.\hello.exe
```

slim 包需要通过 `--clang`、`HITSIMPLE_CLANG` 或 PATH 提供兼容的 llvm-mingw/Clang 18。路径可包含空格和 Unicode 字符；参数通过 argv 传递，不经过 shell quoting。

Windows `f128` 使用软件 binary128 backend，覆盖字面量、算术、比较、数值转换、格式化、扫描和 `math.hsh` 的浮点入口。超越函数的精度和异常标志边界可通过 `--target-info` 查看。

## 发布附件校验

正式 Release 附带 `SHA256SUMS`。下载全部附件后，在 Linux 中运行：

```bash
sha256sum -c SHA256SUMS
```

只有哈希校验全部通过，才能认为下载内容与发布附件一致。
