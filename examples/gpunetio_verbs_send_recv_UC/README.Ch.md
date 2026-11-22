# UC Send/Receive（GPU 直接 doorbell）示例编译与运行指南

本目录提供 Unreliable Connected（UC）two-sided Send/Receive 示例：

```text
examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC
```

与 RC 示例 `examples/gpunetio_verbs_send_recv/` 覆盖范围一致：

- 高级 API：`doca_gpu_dev_verbs_send/recv/recv_wait`
- 低级 API：SQ/RQ WQE 预留、构造、提交及 CQE 轮询
- `SEND` 和 `SEND_IMM`，包括零长度消息
- GPU 直接敲 NIC doorbell（`GPU_SM_DB`）
- CPU 辅助 doorbell（`CPU_PROXY`）
- 标准 `ibv_qp`（`IBV_QPT_UC`）和 custom DEVX（`DOCA_VERBS_QP_TYPE_UC`）后端

GPU 数据路径复用 DOCA 通用 two-sided API；UC 与 RC 的差别在 QP 创建和状态迁移。UC 不提供可靠传输、重传或 RNR 处理，因此示例在任意一端发送前，通过 OOB barrier 确保两端都已提交全部 Recv WQE。

## 1. 环境

本机目标环境：

- Tesla V100-PCIE-32GB，`sm_70`，PCIe 地址 `65:00.0`
- ConnectX-6 Dx，双 RoCE 端口直连
  - `mlx5_0`：`192.168.0.200`
  - `mlx5_1`：`192.168.0.201`
- CUDA 12.2、`libibverbs`、`libmlx5`
- GPU 直接 doorbell 需要 UAR MMIO 映射权限，推荐使用 `sudo`

## 2. 编译

在 repo 根目录执行：

```bash
make clean
make -j4 lib CUDA_ARCH=70
make -j4 examples CUDA_ARCH=70
```

只编译本示例：

```bash
make -C examples/gpunetio_verbs_send_recv_UC \
    TOP_DIR=$PWD CUDA_HOME=/usr/local/cuda CUDA_ARCH=70 -j4
```

## 3. 基本运行：高级 API + GPU 直接 doorbell + ibv

先启动 server，再启动 client。

Server：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv
```

Client：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

成功时两端分别输出：

```text
UC Send/Receive validation successful: high-level API, SEND, 2 iterations x 64 bytes
```

## 4. 参数

| 参数 | 含义 |
|------|------|
| `-c <server_ip>` | client 模式及 server OOB 地址 |
| `-d <mlx5_X>` | RDMA 设备名 |
| `-g <GPU PCIe>` | GPU PCIe 地址 |
| `-l <GID index>` | GID index，本机 RoCEv2 为 `3` |
| `-p <handler>` | `0: AUTO`，`1: CPU_PROXY`，`2: GPU_SM_DB` |
| `-a <high|low>` | 高级或低级 GPU API，默认 `high` |
| `-b <ibv|devx>` | QP 后端，默认 `ibv` |
| `-m` | 使用 `SEND_IMM`，仅允许与 `-a low` 一起使用 |
| `-s <bytes>` | 消息大小，默认 `64`，支持 `0` |
| `-i <iterations>` | ping-pong 次数，默认 `4`，最大 `128` |
| `-r <mode>` | send DBR mode，默认 `0` |

## 5. 对照 RC 的完整测试矩阵

以下命令均以基本运行命令为基础，两端必须使用相同的 `-a`、`-b`、`-m`、`-s` 和 `-i`。

| 后端 | API | NIC handler | Opcode | 参数 |
|------|-----|-------------|--------|------|
| ibv | high | GPU_SM_DB | SEND | `-b ibv -a high -p 2 -s 64` |
| ibv | low | GPU_SM_DB | SEND_IMM | `-b ibv -a low -p 2 -m -s 64` |
| ibv | high | CPU_PROXY | SEND | `-b ibv -a high -p 1 -s 64` |
| ibv | low | CPU_PROXY | SEND_IMM | `-b ibv -a low -p 1 -m -s 64` |
| devx | high | GPU_SM_DB | SEND | `-b devx -a high -p 2 -s 64` |
| devx | low | GPU_SM_DB | SEND_IMM | `-b devx -a low -p 2 -m -s 64` |
| devx | high | CPU_PROXY | SEND | `-b devx -a high -p 1 -s 64` |
| devx | low | CPU_PROXY | SEND_IMM | `-b devx -a low -p 1 -m -s 64` |

零长度 SEND/SEND_IMM 覆盖：将 `-s 64` 改为 `-s 0`。

低级 API + GPU doorbell + SEND_IMM 的完整命令示例：

```bash
# server
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a low -m -b ibv

# client
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a low -m -b ibv \
    -c 192.168.0.200
```

CPU proxy 测试将两端 `-p 2` 改为 `-p 1`；DEVX 测试将两端 `-b ibv` 改为 `-b devx`。

## 6. 示例覆盖的 GPU API

高级路径：

- `doca_gpu_dev_verbs_recv`
- `doca_gpu_dev_verbs_send`
- `doca_gpu_dev_verbs_recv_wait`
- `doca_gpu_dev_verbs_cqe_get_bytes`
- `doca_gpu_dev_verbs_cqe_get_imm`

低级路径：

- `doca_gpu_dev_verbs_reserve_wq_slots`
- `doca_gpu_dev_verbs_get_wqe_ptr` / `doca_gpu_dev_verbs_get_rwqe_ptr`
- `doca_gpu_dev_verbs_wqe_prepare_send` / `doca_gpu_dev_verbs_wqe_prepare_recv`
- `doca_gpu_dev_verbs_mark_wqes_ready`
- `doca_gpu_dev_verbs_submit`
- `doca_gpu_dev_verbs_poll_cq` / `doca_gpu_dev_verbs_poll_cq_at`

`SEND_IMM` 的 immediate 值和接收 CQE 中的 immediate 值会一起校验。

## 7. 注意事项与排错

1. **先提交 Recv**：UC 不会像 RC 一样进行 RNR 重试；接收方未准备好时消息可能丢失。不要移除示例中的 pre-post 和 OOB ready barrier。
2. **UC 状态迁移属性**：不能使用 RC 专用的 remote-read、atomic、retry、RNR 和 RD-atomic 属性。本示例的 ibv 和 DEVX 路径均按 UC 规则设置属性。
3. **独立 SQ/RQ CQ**：`ibv_scq` 与 `ibv_rcq` 分开创建，DEVX 路径同样使用独立 CQ。
4. **V100 MCST dump**：接收 buffer 读取前按 `need_mcst` 执行 dump，保证 GPU 内存一致性。
5. **权限**：`cudaHostRegisterIoMemory ... operation not permitted` 时，使用 `sudo` 并检查 UAR 映射环境。
6. **启动顺序**：先启动 server；`Unable to connect to server` 通常表示 server 尚未 listen。
7. **无重传语义**：网络丢包、Recv 不足或两端参数不一致可能造成永久等待，这不等同于 RC 的自动恢复行为。
8. **可选 host RX**：设置 `SEND_RECV_UC_HOST_RX=1` 可让接收 buffer 使用 mapped pinned host memory。
9. **库路径**：若出现 `libdoca_gpunetio_host.so.3: cannot open shared object file`，设置 `LD_LIBRARY_PATH="$PWD/lib"`。
10. **OOB 端口隔离**：默认 TCP 端口为 `2000`。并行运行多组测试或需要避开残留 server 时，在两端同时设置不同的 `GPUNETIO_OOB_PORT`，例如 `GPUNETIO_OOB_PORT=2301`。
