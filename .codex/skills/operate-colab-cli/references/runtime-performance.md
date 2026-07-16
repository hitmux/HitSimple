# Colab runtime 实测性能

本文记录 `google-colab-cli 0.6.0` 在 2026-07-12 的单次快速实测，用于选择 runtime 和识别普通 RAM、High-RAM 实际分配。Colab 的硬件、区域、配额和可用性会变化；不要把本文数值当作固定规格或服务承诺。

## 汇总

| CLI 请求 | 实际硬件 | CPU 核心/线程 | RAM | `/content` 总容量 | 显存 | CPU 矩阵 | GPU 矩阵 | 磁盘写/读 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU | Intel Xeon 2.20 GHz | 1/2 | 12.67 GiB | 225.83 GiB | - | 66.3 GFLOPS | - | 668 MB/s / 2.9 GB/s |
| CPU High-RAM | Intel Xeon 2.20 GHz | 4/8 | 50.99 GiB | 225.83 GiB | - | 263.4 GFLOPS | - | 1.1 GB/s / 4.5 GB/s |
| T4 | Tesla T4 | 1/2 | 12.67 GiB | 235.63 GiB | 15.00 GiB | 119.9 GFLOPS | 3.40 TFLOPS | 1.7 GB/s / 4.3 GB/s |
| T4 High-RAM | Tesla T4 | 4/8 | 50.99 GiB | 235.63 GiB | 15.00 GiB | 403.2 GFLOPS | 4.01 TFLOPS | 1.9 GB/s / 5.0 GB/s |
| L4 | NVIDIA L4 | 6/12 | 52.96 GiB | 235.63 GiB | 22.49 GiB | 502.9 GFLOPS | 28.69 TFLOPS | 1.7 GB/s / 4.4 GB/s |
| G4 | NVIDIA RTX PRO 6000 Blackwell Server Edition | 24/48 | 176.88 GiB | 235.63 GiB | 95.59 GiB | 1493.5 GFLOPS | 205.03 TFLOPS | 4.6 GB/s / 16.7 GB/s |
| A100 | 未分配 | - | - | - | - | not verified | not verified | not verified |
| H100 | 未分配 | - | - | - | - | not verified | not verified | not verified |

表中的 CPU 核心数为物理核心数，线程数为 `os.cpu_count()` 看到的逻辑 CPU 数。

## Runtime 详细信息

### CPU

- `status`：`Hardware: CPU | Variant: DEFAULT`
- CPU：`Intel(R) Xeon(R) CPU @ 2.20GHz`
- CPU 拓扑：1 socket、1 core、2 threads
- RAM：`13286936 kB`，约 12.67 GiB
- `/content`：225.83 GiB，总可用空间约 205.87 GiB
- 系统：Linux kernel `6.6.122+`，x86_64

### CPU High-RAM

- 命令：`colab new -s high-ram-cpu --high-ram`
- `status`：`Hardware: CPU | Variant: DEFAULT | Shape: HIGH_RAM`
- CPU：`Intel(R) Xeon(R) CPU @ 2.20GHz`
- CPU 拓扑：1 socket、4 cores、8 threads
- RAM：`53467184 kB`，约 50.99 GiB
- `/content`：225.83 GiB，总可用空间约 205.87 GiB
- CPU 矩阵：263.4 GFLOPS
- 磁盘：写入 1.1 GB/s，读取 4.5 GB/s

### T4

- `status`：`Hardware: T4 | Variant: GPU`
- GPU：`Tesla T4`
- 显存：`15360 MiB`
- Compute Capability：7.5
- CPU：`Intel(R) Xeon(R) CPU @ 2.00GHz`，1 core / 2 threads
- RAM：`13286936 kB`，约 12.67 GiB
- `/content`：235.63 GiB，总可用空间约 188.98 GiB
- NVIDIA driver：`580.82.07`
- PyTorch：`2.11.0+cu128`
- CUDA runtime：12.8

### T4 High-RAM

- 命令：`colab new -s high-ram-t4 --gpu T4 --high-ram`
- `status`：`Hardware: T4 | Variant: GPU | Shape: HIGH_RAM`
- GPU：`Tesla T4`
- 显存：`15360 MiB`
- Compute Capability：7.5
- CPU：`Intel(R) Xeon(R) CPU @ 2.00GHz`，4 cores / 8 threads
- RAM：`53467192 kB`，约 50.99 GiB
- `/content`：235.63 GiB，总可用空间约 188.98 GiB
- CPU 矩阵：403.2 GFLOPS
- GPU 矩阵：4.01 TFLOPS
- 磁盘：写入 1.9 GB/s，读取 5.0 GB/s

### L4

- `status`：`Hardware: L4 | Variant: GPU`
- GPU：`NVIDIA L4`
- 显存：`23034 MiB`，约 22.49 GiB
- Compute Capability：8.9
- CPU：`Intel(R) Xeon(R) CPU @ 2.20GHz`，6 cores / 12 threads
- RAM：`55530592 kB`，约 52.96 GiB
- `/content`：235.63 GiB，总可用空间约 188.98 GiB
- NVIDIA driver：`580.82.07`
- PyTorch：`2.11.0+cu128`
- CUDA runtime：12.8

### G4

- `status`：`Hardware: G4 | Variant: GPU`
- 实际 GPU：`NVIDIA RTX PRO 6000 Blackwell Server Edition`
- 显存：`97887 MiB`，约 95.59 GiB
- Compute Capability：12.0
- CPU：`AMD EPYC 9B45`，24 cores / 48 threads
- RAM：`185477148 kB`，约 176.88 GiB
- `/content`：235.63 GiB，总可用空间约 188.98 GiB
- NVIDIA driver：`580.82.07`
- PyTorch：`2.11.0+cu128`
- CUDA runtime：12.8

`G4` 是 CLI 的请求名称，本次实际分配的设备名称是 RTX PRO 6000 Blackwell Server Edition。后续使用时必须重新运行 `nvidia-smi`，不要只根据 `G4` 名称推断具体 GPU。

### A100 和 H100

- A100 连续两次申请均返回 `ColabRequestError: ... accelerator=A100: Service Unavailable`，没有创建 runtime，性能为 `not verified`。
- H100 返回 `Backend rejected accelerator 'H100'. You may not have quota or entitlement for this accelerator on your account.`，没有创建 runtime，性能为 `not verified`。
- 失败后运行 `colab sessions`，服务端没有残留 session。

## RAM 类型

上游 CLI 0.6.0 没有暴露普通 RAM 或 High-RAM 参数，但源码已定义服务端响应字段 `machineShape`。Colab Web 前端的实际请求契约为：

- 旧 Tunnel Frontend：在 `/tun/m/assign` URL 中添加 `shape=hm`。
- 新 RuntimeService RPC：将 `machineShape` 设为 2；标准规格为 1。

当前宿主机的 CLI 0.6.0 已本地补充 `colab new --high-ram` 和 `colab run --high-ram`，并让 `sessions/status` 显示服务端实际 `Shape`。当前已使用 pipx pin 锁定 0.6.0，关闭 CLI 后台更新检查，并禁用 `colab update --install`；手动 `pip install` 或重新安装仍会覆盖 pipx venv 内的补丁。

本次观察到以下 RAM 档位：

- 普通 RAM：CPU 和 T4 均约 12.67 GiB。
- 显式 High-RAM：CPU 和 T4 均约 50.99 GiB，同时从 1 core / 2 threads 增加到 4 cores / 8 threads。
- 较高 RAM：L4 约 52.96 GiB。
- 大 RAM：G4 约 176.88 GiB。

Colab Web 前端会对 L4、G4、H100 和 TPU 使用固定 machine shape，不通过 High-RAM 开关改变规格。A100 没有被前端列为固定 shape，但本次账号无法成功分配 A100，因此未验证。重新申请任何型号时，仍需同时检查 `Shape` 和 `/proc/meminfo`。

## 测试方法

- CPU：NumPy `float32` 2048 x 2048 矩阵乘法，按 `2 * n^3 / elapsed` 估算吞吐量。
- GPU：PyTorch `float32` 8192 x 8192 矩阵乘法，预热 2 次、执行 10 次，允许 TF32，按 `2 * n^3 * repeats / elapsed` 估算吞吐量。
- 磁盘：`dd` direct I/O；GPU runtime 写入和读取 256 MiB，CPU runtime 写入和读取 512 MiB。
- GPU 核验：同时检查 CLI `status`、`nvidia-smi`、`torch.cuda.is_available()` 和 PyTorch 实际设备名称。

GPU 测试允许 TF32，因此 Ampere、Ada、Blackwell 与 T4 的数值反映 PyTorch 默认可用的实际矩阵吞吐能力，不是严格 IEEE FP32 的同精度理论性能比较。磁盘测试文件较小，结果容易受宿主机和缓存影响。

## 选型提示

- 只需 CUDA 兼容性验证或小型实验时，优先尝试 T4。
- 需要更强矩阵性能和较大 RAM 时，优先尝试 L4，并核验实际 RAM。
- 需要超大显存或大 RAM 时，可以尝试 G4；本次规格和吞吐明显高于 L4，配额消耗也应单独评估。
- A100、H100 必须以当前账号的实际分配结果为准；申请失败时不要静默替换型号。
