---
name: operate-colab-cli
description: 使用 Google Colab CLI 申请、检查、操作和释放 CPU/GPU/TPU runtime，并执行 Python、Notebook、REPL、远程 TTY、文件传输、依赖安装和日志检查。用于 Colab 实验、GPU 训练、临时算力任务、把宿主机现有 rclone 配置临时上传到 VM 以直接挂载 Google Drive，以及要求任务结束前归档重要数据并确认实例不再占用资源的场景。
---

# 操作 Colab CLI

把 Colab runtime 视为临时计算环境。将 `/content` 中需要保留的结果下载到持久化存储，并在每次任务结束后停止实例、查询服务端会话列表复核。

## 开始前检查

1. 运行 `command -v colab` 和 `colab version` 确认 CLI。
2. 运行 `colab sessions` 验证认证并盘点服务端已有实例。不要仅检查本地进程或 session state 文件。
3. 运行 `colab <command> --help` 核对当前版本参数。全局选项必须放在子命令之前，例如 `colab --auth=adc sessions`。
4. 为实例指定唯一名称。不要在多个会话存在时依赖 CLI 自动选择。
5. 运行本 Skill 的脚本前先切换到项目根目录，再使用 `.codex/skills/operate-colab-cli/scripts/...` 相对路径。
6. 检查用一行命令即可完成

当前宿主机的 CLI 0.6.0 已本地增加 `--high-ram`，并通过 pipx pin、关闭后台更新检查、禁用 `colab update --install` 锁定版本。手动执行 `pip install` 或重新安装仍可能覆盖补丁；每次使用前继续以 `colab version`、`colab new --help` 和 `colab run --help` 为准。

## Runtime 性能参考

需要比较 CPU、GPU、RAM、显存和临时磁盘规格，或选择合适的 GPU 型号时，先阅读 [Colab runtime 实测性能](references/runtime-performance.md)。参考数据来自单次实际分配，只用于选型；每次任务仍要通过 `status`、系统信息和 `nvidia-smi` 核验当前实例。

## 申请和核验实例

申请 CPU Only runtime 时不要传 `--gpu` 或 `--tpu`：

```bash
colab new -s experiment-cpu
colab status -s experiment-cpu
```

只有 `status` 明确显示 `Hardware: CPU` 才报告 CPU 实例已就绪。GPU 和 TPU 分别使用当前 `colab new --help` 列出的 `--gpu`、`--tpu` variant；不要猜测型号或配额。

### 申请 High-RAM 实例

当前本地补丁将 `--high-ram` 映射为 Colab Tunnel Frontend 的 `shape=hm` 参数：

```bash
colab new -s experiment-cpu-hm --high-ram
colab new -s experiment-t4-hm --gpu T4 --high-ram
colab status -s experiment-t4-hm
```

只有服务端状态显示 `Shape: HIGH_RAM`，并且 `/proc/meminfo` 显示预期容量时，才能报告 High-RAM 分配成功。`L4`、`G4`、`H100` 和 TPU 在 Colab Web 前端中使用固定 machine shape，不要依靠 `--high-ram` 改变其 RAM；直接核验实际规格。A100 是否支持及是否可分配取决于当前账号和后端。

一次性 High-RAM 任务使用：

```bash
colab run --high-ram script.py
colab run --gpu T4 --high-ram train.py
```

## 申请和验证 GPU 实例

当你认为需要使用 GPU 实例时再考虑.有些任务不需要 GPU, 或者并不适合 GPU
在用 GPU前, 为了避免在 GPU 实例上浪费太多时间, 可以先在 CPU 实例上面测试, 如果测试必须要用 GPU, 可以先考虑低价 GPU 进行测试.
Colab CLI 0.6.0 列出 `T4`、`L4`、`G4`、`H100` 和 `A100`，参数值区分大小写。GPU 型号影响配额和 compute units；用户未指定型号时先询问，不要擅自申请高价型号。

申请指定型号并立即检查状态：

```bash
colab new -s experiment-t4 --gpu T4
colab status -s experiment-t4
```

CLI 0.6.0 的 T4 实测状态格式为 `Hardware: T4 | Variant: GPU`。只有 `Hardware` 等于请求型号且 `Variant` 为 `GPU`，才能报告目标 GPU 已分配。随后在 VM 内验证驱动和实际设备：

```bash
colab exec -s experiment-t4 --timeout 60 <<'PY'
import subprocess

subprocess.run([
    'nvidia-smi',
    '--query-gpu=name,memory.total,driver_version',
    '--format=csv,noheader',
], check=True)

try:
    import torch
except ImportError:
    print('PyTorch not installed; nvidia-smi verification passed.')
else:
    print('torch.cuda.is_available=', torch.cuda.is_available())
    if torch.cuda.is_available():
        print('torch_device=', torch.cuda.get_device_name(0))
PY
```

`status` 验证的是分配结果，`nvidia-smi` 验证 VM 内的 GPU 和驱动，框架检查验证 Python 工作负载是否能使用 CUDA。不要用其中单独一项代替完整判断。

申请失败、型号不可用或账号没有 entitlement 时，先运行 `colab sessions` 确认没有残留实例，再报告原始错误。不要静默改用其他 GPU、CPU，也不要连续申请多个昂贵型号；让用户选择重试同型号、改用其他型号或 CPU。

一次性 GPU 脚本使用：

```bash
colab run -s one-shot-t4 --gpu T4 --timeout 300 train.py
```

`run` 默认在脚本结束或报错后释放实例。只有需要继续检查环境时才使用 `--keep`；使用后必须按普通 session 执行数据归档和停止流程。

## 执行工作负载

执行标准 Python 代码：

```bash
printf '%s\n' "print('hello from Colab')" | colab exec -s experiment-cpu
colab exec -s experiment-cpu -f local_script.py --timeout 300
```

`-f` 会读取本地 `.py` 或 `.ipynb` 并发送到远端 kernel，不要求提前上传。相同 session 的 kernel 状态会跨多次 `exec`/`repl` 保留；需要清空状态时运行：

```bash
colab restart-kernel -s experiment-cpu
```

需要交互式 Python 时运行 `colab repl -s experiment-cpu`。自动化环境可通过 stdin 输入。需要真实 shell 时运行：

```bash
colab console -s experiment-cpu
```

`console` 使用 TTY/tmux，会产生终端控制字符；退出 shell 只断开控制台，不释放 runtime。使用 `colab url -s experiment-cpu` 获取连接同一实例的 Colab Web URL。

## 管理文件和依赖

```bash
colab ls -s experiment-cpu /content
colab upload -s experiment-cpu ./input.dat /content/input.dat
colab download -s experiment-cpu /content/result.dat ./result.dat
colab rm -s experiment-cpu /content/input.dat
colab install -s experiment-cpu numpy pandas
```

下载后校验重要产物的大小或 SHA-256。只删除本次任务创建的远端文件。

## 使用持久化 Google Drive

根据数据规模和授权条件选择路径：

- 小型私有文件、配置和结果归档：使用宿主机 `~/gdrive` 与 `colab upload/download`，便于无人值守，但文件数据会经过宿主机。
- 大型私有数据集：使用 VM 内 `colab drivemount`，文件数据在 Google Drive 与 Colab VM 之间直接传输，但需要用户授权。
- 用户明确接受把现有 rclone refresh token 临时复制到 VM：上传 rclone 和配置，在 VM 内直接挂载；不需要浏览器交互，文件数据不经过宿主机。
- 公开或“知道链接即可访问”的文件：让 VM 直接从公开 URL 下载，无需 Drive 账号授权，文件数据不经过宿主机。
- 无人值守访问私有 Drive：必须使用 service account、OAuth token 等凭据；仍然属于授权。只有用户明确许可并指定安全的 secret 存储方式后才配置。

使用宿主机持久化挂载前，先确认 `~/gdrive` 确实是挂载点：

```bash
mountpoint "$HOME/gdrive"
findmnt -T "$HOME/gdrive"
```

从 Drive 向 Colab 发送数据，或把结果直接写回 Drive：

```bash
colab upload -s experiment-cpu "$HOME/gdrive/experiments/input.dat" /content/input.dat
colab download -s experiment-cpu /content/result.dat "$HOME/gdrive/experiments/result.dat"
```

这种方式让长期 OAuth/refresh token 留在宿主机，Colab VM 只接触传输的文件，但数据路径会经过宿主机。不要把 `~/.config/rclone/rclone.conf`、refresh token 或其他长期凭据上传到 VM，除非用户明确授权并接受风险。

需要在 VM 内直接获得 `/content/drive` 时运行：

```bash
colab drivemount -s experiment-cpu /content/drive
```

该命令需要人工浏览器授权，并使用当前 runtime 的 `dfs_ephemeral` 凭据。挂载不会跨实例保留；数据传输不经过宿主机。不要在无人值守任务中等待授权。CLI 登录、VM 侧 `colab auth` 和 Drive 授权是三套不同流程，不要混用。私有 Drive 不存在完全无授权的访问方式。

### 复用宿主机现有 rclone 配置

只有用户明确授权复制现有长期凭据后才执行。该方案会让 Colab VM 获得配置中 `Google:` remote 的完整权限；不要打印、记录或导出 `rclone.conf` 内容。凭据可以保留在 VM 内直到实例释放，临时磁盘会随实例销毁。

运行随 Skill 提供的挂载脚本：

```bash
bash .codex/skills/operate-colab-cli/scripts/mount-rclone-drive.sh experiment-cpu
```

脚本默认上传本机 `rclone` 与 `~/.config/rclone/rclone.conf`，将 `Google:` 根目录挂载到 `/content/drive/MyDrive`，兼容 Colab 原生 Drive 的常见路径。可通过 `RCLONE_BINARY`、`RCLONE_CONFIG`、`RCLONE_REMOTE` 和 `RCLONE_MOUNT_PATH` 覆盖默认值。

挂载后运行以下检查，不要通过列出私人文件名来证明成功：

```bash
colab exec -s experiment-cpu <<'PY'
from pathlib import Path
print(Path('/content/drive/MyDrive').is_mount())
PY
```

已在 Colab CPU Ubuntu 22.04 runtime 验证 `/dev/fuse`、`fusermount` 和 rclone FUSE mount 可用。仍要在每个新实例上检查实际结果。上传阶段会经过宿主机；挂载完成后的 Drive 文件读写发生在 Google Drive 与 Colab VM 之间。这里直接把 remote 根目录放在 `MyDrive`，不要再访问 `/content/drive/MyDrive/MyDrive`。

## 运行一次性任务

使用 `run` 自动完成申请、执行和释放：

```bash
colab run -s one-shot-cpu script.py arg1 arg2
```

CPU 任务不要传 accelerator 参数。默认在脚本结束后释放实例；只有确实需要保留环境时才使用 `--keep`，并记录后续必须执行的 `stop`。

## 查看状态和日志

```bash
colab status -s experiment-cpu
colab log -s experiment-cpu -n 30
colab sessions
```

执行异常时先看 `log`，再决定重启 kernel 或重建实例。`status` 中的本地 `running` 标记可能因客户端异常中断而过期；资源是否仍分配以服务端 `colab sessions` 为准。

## 关闭前保存重要数据

关闭实例前先确认实验不再需要继续运行，并盘点 `/content` 中需要保留的模型、日志、数据和 Notebook。将重要目录打成 tar 包，再使用 VM 内的 rclone 直接复制到 Google Drive：

```bash
colab console -s experiment-cpu
tar -czf /content/experiment-result.tar.gz -C /content project-directory
/content/.colab-rclone copyto /content/experiment-result.tar.gz Google:ColabArchives/experiment-result.tar.gz --config=/content/.colab-rclone.conf
/content/.colab-rclone lsf Google:ColabArchives --config=/content/.colab-rclone.conf --include=experiment-result.tar.gz
exit
```

只有归档命令成功且 Drive 目标可查到，才能视为重要数据已经保存。没有重要数据时明确记录无需归档。

## 停止实例

任务结束、发生错误、等待人工输入失败或用户中断时，都要停止实例。使用随 Skill 提供的简化脚本：

```bash
bash .codex/skills/operate-colab-cli/scripts/stop-and-verify.sh experiment-cpu
```

手动流程为：

```bash
colab stop -s experiment-cpu
colab sessions
```

脚本只执行一次 `stop` 和一次 `sessions` 检查。服务端列表中不再出现该 session 后报告资源已释放。不要把关闭 console 或 kernel 变为 IDLE 当作实例已释放。
