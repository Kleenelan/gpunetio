# RC Send/Receive（GPU 直接 doorbell）示例编译与运行指南

本文介绍本 repo 新增的 RC Send/Receive 示例：

```text
examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv
```

示例同时覆盖：

- 高级 API：`doca_gpu_dev_verbs_send/recv/recv_wait`
- 低级 API：RQ/SQ WQE 预留、构造、提交及 CQE 轮询
- `SEND` 和 `SEND_IMM`
- GPU 直接敲 NIC doorbell（`GPU_SM_DB`）
- CPU 辅助 doorbell（`CPU_PROXY`）
- 标准 `ibv_qp` 后端和 DEVX 后端

---

## 1. 环境与依赖

本机验证环境：

- GPU：Tesla V100-PCIE-32GB，`sm_70`，PCIe 地址 `65:00.0`
- NIC：ConnectX-6 Dx，双 RoCE 端口光纤直连
  - `mlx5_0`：`192.168.0.200`
  - `mlx5_1`：`192.168.0.201`
- CUDA：12.2
- rdma-core：`libibverbs`、`libmlx5`
- 使用 `sudo` 运行，以满足 UAR MMIO 注册权限要求

GPU 直接敲 doorbell 时，必须能成功执行 `cudaHostRegisterIoMemory`。普通用户可能遇到 `operation not permitted`，因此推荐使用 `sudo`。

---

## 2. 编译

在 repo 根目录执行：

```bash
# 如需清理旧对象，避免头文件变更后复用旧对象
make clean

# 先编译 host 库，再编译所有 examples
make -j4 lib CUDA_ARCH=70
make -j4 examples CUDA_ARCH=70
```

只编译该示例：

```bash
make -C examples/gpunetio_verbs_send_recv \
    TOP_DIR=$PWD CUDA_HOME=/usr/local/cuda CUDA_ARCH=70 -j4
```

生成的程序：

```text
examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv
```

---

## 3. 基本运行方式

示例采用 client/server 结构。先启动 server，再启动 client。

### 3.1 设置环境

```bash
cd /home/hongleili/ex_doca/tmp03_gpunetio_UC_send_receive_impl/gpunetio
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"
export DOCA_GPUNETIO_LOG=6
```

如希望减少日志：

```bash
export DOCA_GPUNETIO_LOG=3
```

### 3.2 Server：`mlx5_0`

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv
```

### 3.3 Client：`mlx5_1`

server 启动后执行：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

`192.168.0.200` 是 server 的 OOB 地址。两台主机之间运行时，应将其替换为 server 主机的实际 OOB IP。

成功时两端分别输出类似：

```text
Send/Receive validation successful: high-level API, SEND, 2 iterations x 64 bytes
```

---

## 4. 参数说明

| 参数 | 含义 |
|------|------|
| `-c <server_ip>` | 以 client 模式运行，并连接 server 的 OOB 地址 |
| `-d <mlx5_X>` | RDMA 设备名，如 `mlx5_0`、`mlx5_1` |
| `-g <GPU PCIe>` | GPU PCIe 地址，本机为 `65:00.0` |
| `-l <GID index>` | GID index；本机 RoCEv2 使用 `3` |
| `-p <handler>` | `0: AUTO`，`1: CPU_PROXY`，`2: GPU_SM_DB` |
| `-a <high|low>` | 高级或低级 GPU API，默认 `high` |
| `-b <ibv|devx>` | QP 后端，默认 `ibv` |
| `-m` | 使用 `SEND_IMM`，仅允许与 `-a low` 一起使用 |
| `-s <bytes>` | 消息大小，默认 `64`，支持 `0` |
| `-i <iterations>` | ping-pong 迭代次数，默认 `4`，最大 `128` |
| `-r <mode>` | send DBR mode，默认 `0` |

注意：`-m` 只能和 `-a low` 一起使用。

---

## 5. 推荐测试矩阵

### 5.1 高级 API + GPU 直接 doorbell + SEND

Server：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv
```

Client：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

### 5.2 低级 API + GPU 直接 doorbell + SEND_IMM

Server：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a low -m -b ibv
```

Client：

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a low -m -b ibv \
    -c 192.168.0.200
```

### 5.3 高级 API + CPU proxy + SEND

将 `-p 2` 改为 `-p 1`：

```bash
# server
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 1 -i 2 -s 64 -a high -b ibv

# client
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 1 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

### 5.4 低级 API + CPU proxy + SEND_IMM

```bash
# server
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 1 -i 2 -s 64 -a low -m -b ibv

# client
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 1 -i 2 -s 64 -a low -m -b ibv \
    -c 192.168.0.200
```

### 5.5 零长度 SEND

将 `-s 64` 改为 `-s 0`，用于覆盖 zero-byte send 路径。

---

## 6. 示例使用的 API

### 高级路径（`-a high`）

- `doca_gpu_dev_verbs_recv`
- `doca_gpu_dev_verbs_send`
- `doca_gpu_dev_verbs_recv_wait`
- `doca_gpu_dev_verbs_cqe_get_bytes`
- `doca_gpu_dev_verbs_cqe_get_imm`

### 低级路径（`-a low`）

- `doca_gpu_dev_verbs_reserve_wq_slots`
- `doca_gpu_dev_verbs_get_wqe_ptr`
- `doca_gpu_dev_verbs_get_rwqe_ptr`
- `doca_gpu_dev_verbs_wqe_prepare_send`
- `doca_gpu_dev_verbs_wqe_prepare_recv`
- `doca_gpu_dev_verbs_mark_wqes_ready`
- `doca_gpu_dev_verbs_submit`
- `doca_gpu_dev_verbs_poll_cq`
- `doca_gpu_dev_verbs_poll_cq_at`

`SEND_IMM` 的 immediate 值和接收 CQE 中的 immediate 值会一起校验。

---

## 7. 后端选择

### `-b ibv`（推荐）

使用标准 `ibv_create_qp` 创建 RC QP，再通过：

```c
doca_gpu_verbs_ibv_export_qp()
```

导出至 GPU。当前本机已验证：

- `high + SEND + GPU_SM_DB`
- `low + SEND_IMM + GPU_SM_DB`
- `high + SEND + CPU_PROXY`
- `low + SEND_IMM + CPU_PROXY`

### `-b devx`

使用 repo 原有的 custom DEVX QP 创建路径。该后端也已在本机通过 direct/proxy、high/low 验证。默认示例后端为 `ibv`，需要验证 custom DEVX 时可显式使用 `-b devx`。

---

## 8. 本机注意事项

1. **必须使用独立 SQ/RQ CQ**：`ibv_scq` 与 `ibv_rcq` 分开创建，避免 SEND、RECV 和 MCST dump 的 CQE 混淆。
2. **V100 需要 MCST dump**：读取接收 buffer 前，示例会根据 `need_mcst` 执行 dump，保证 GPU 内存一致性。
3. **sudo 用于 UAR 映射**：GPU 直接 doorbell 需要 MMIO 映射权限。
4. **GDRCopy 不是该示例的必要条件**：仅其他 `GPU_CPU` 或 no-DBR 特性需要。若使用这些特性，需确保 `/dev/gdrdrv` 存在。
5. **OOB 启动顺序**：必须先启动 server，再启动 client；若 client 连接过早，会出现 `Unable to connect to server`。
6. **Stale object 问题**：修改公共头文件后建议执行 `make clean`，避免不同编译单元使用不同结构体布局。

---

## 9. 常见问题

### `cudaHostRegister ... operation not permitted`

使用 `sudo` 运行，并确保使用 `-p 2` 时系统允许 UAR MMIO 映射。

### `Unable to connect to server`

server 尚未完成初始化并开始 listen。增加 client 启动延迟，或先确认 server 日志出现：

```text
Listening for incoming connections
```

### `Send/Receive validation failed`

先检查：

- 两端 `-a`、`-b`、`-m`、`-s`、`-i` 是否一致；
- GID index 是否正确（本机为 `3`）；
- 是否使用独立 SQ/RQ CQ；
- 是否使用 sudo；
- 是否发生了实际 CQE 错误。可设置 `DOCA_GPUNETIO_LOG=6` 查看详细日志。

### `libdoca_gpunetio_host.so.3: cannot open shared object file`

检查：

```bash
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"
```
