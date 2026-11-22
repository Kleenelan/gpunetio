# UC Send/Receive Example with GPU-Direct Doorbells: Build and Run Guide

This directory provides an Unreliable Connected (UC) two-sided Send/Receive example:

```text
examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC
```

It provides the same coverage as the RC example in `examples/gpunetio_verbs_send_recv/`:

- High-level APIs: `doca_gpu_dev_verbs_send/recv/recv_wait`
- Low-level APIs: SQ/RQ WQE reservation, construction, submission, and CQE polling
- `SEND` and `SEND_IMM`, including zero-length messages
- Ringing NIC doorbells directly from the GPU (`GPU_SM_DB`)
- CPU-assisted doorbells (`CPU_PROXY`)
- Standard `ibv_qp` (`IBV_QPT_UC`) and custom DEVX (`DOCA_VERBS_QP_TYPE_UC`) backends

The GPU data path reuses DOCA's generic two-sided APIs. UC differs from RC in QP creation and state transitions. UC does not provide reliable delivery, retransmission, or RNR handling. Therefore, before either endpoint sends, the example uses an OOB barrier to ensure that both endpoints have posted all their Recv WQEs.

## 1. Environment

Target local environment:

- Tesla V100-PCIE-32GB, `sm_70`, PCIe address `65:00.0`
- ConnectX-6 Dx with two directly connected RoCE ports
  - `mlx5_0`: `192.168.0.200`
  - `mlx5_1`: `192.168.0.201`
- CUDA 12.2, `libibverbs`, and `libmlx5`
- GPU-direct doorbells require UAR MMIO mapping permission; using `sudo` is recommended

## 2. Build

Run the following commands from the repository root:

```bash
make clean
make -j4 lib CUDA_ARCH=70
make -j4 examples CUDA_ARCH=70
```

To build only this example:

```bash
make -C examples/gpunetio_verbs_send_recv_UC \
    TOP_DIR=$PWD CUDA_HOME=/usr/local/cuda CUDA_ARCH=70 -j4
```

## 3. Basic Run: High-Level API + GPU-Direct Doorbells + ibv

Start the server before the client.

Server:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv
```

Client:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv_UC/gpunetio_verbs_send_recv_UC \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

On success, each endpoint prints:

```text
UC Send/Receive validation successful: high-level API, SEND, 2 iterations x 64 bytes
```

## 4. Command-Line Options

| Option | Description |
|--------|-------------|
| `-c <server_ip>` | Client mode and server OOB address |
| `-d <mlx5_X>` | RDMA device name |
| `-g <GPU PCIe>` | GPU PCIe address |
| `-l <GID index>` | GID index; RoCEv2 uses `3` on this machine |
| `-p <handler>` | `0: AUTO`, `1: CPU_PROXY`, `2: GPU_SM_DB` |
| `-a <high|low>` | High-level or low-level GPU API; default: `high` |
| `-b <ibv|devx>` | QP backend; default: `ibv` |
| `-m` | Use `SEND_IMM`; only valid together with `-a low` |
| `-s <bytes>` | Message size; default: `64`; `0` is supported |
| `-i <iterations>` | Number of ping-pong iterations; default: `4`; maximum: `128` |
| `-r <mode>` | Send DBR mode; default: `0` |

## 5. Complete Test Matrix Matching the RC Example

All commands below are based on the basic run command. Both endpoints must use the same `-a`, `-b`, `-m`, `-s`, and `-i` options.

| Backend | API | NIC handler | Opcode | Options |
|---------|-----|-------------|--------|---------|
| ibv | high | GPU_SM_DB | SEND | `-b ibv -a high -p 2 -s 64` |
| ibv | low | GPU_SM_DB | SEND_IMM | `-b ibv -a low -p 2 -m -s 64` |
| ibv | high | CPU_PROXY | SEND | `-b ibv -a high -p 1 -s 64` |
| ibv | low | CPU_PROXY | SEND_IMM | `-b ibv -a low -p 1 -m -s 64` |
| devx | high | GPU_SM_DB | SEND | `-b devx -a high -p 2 -s 64` |
| devx | low | GPU_SM_DB | SEND_IMM | `-b devx -a low -p 2 -m -s 64` |
| devx | high | CPU_PROXY | SEND | `-b devx -a high -p 1 -s 64` |
| devx | low | CPU_PROXY | SEND_IMM | `-b devx -a low -p 1 -m -s 64` |

Zero-length SEND/SEND_IMM coverage: replace `-s 64` with `-s 0`.

Full command example for the low-level API + GPU doorbells + SEND_IMM:

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

For CPU proxy tests, replace `-p 2` with `-p 1` on both endpoints. For DEVX tests, replace `-b ibv` with `-b devx` on both endpoints.

## 6. GPU APIs Covered by the Example

High-level path:

- `doca_gpu_dev_verbs_recv`
- `doca_gpu_dev_verbs_send`
- `doca_gpu_dev_verbs_recv_wait`
- `doca_gpu_dev_verbs_cqe_get_bytes`
- `doca_gpu_dev_verbs_cqe_get_imm`

Low-level path:

- `doca_gpu_dev_verbs_reserve_wq_slots`
- `doca_gpu_dev_verbs_get_wqe_ptr` / `doca_gpu_dev_verbs_get_rwqe_ptr`
- `doca_gpu_dev_verbs_wqe_prepare_send` / `doca_gpu_dev_verbs_wqe_prepare_recv`
- `doca_gpu_dev_verbs_mark_wqes_ready`
- `doca_gpu_dev_verbs_submit`
- `doca_gpu_dev_verbs_poll_cq` / `doca_gpu_dev_verbs_poll_cq_at`

The example validates both the `SEND_IMM` immediate value and the immediate value in the receive CQE.

## 7. Notes and Troubleshooting

1. **Post Recv first**: UC does not perform RNR retries like RC. Messages may be lost if the receiver is not ready. Do not remove the example's pre-post operation or OOB ready barrier.
2. **UC state transition attributes**: Do not use RC-specific remote-read, atomic, retry, RNR, or RD-atomic attributes. Both the ibv and DEVX paths in this example set attributes according to UC rules.
3. **Separate SQ/RQ CQs**: `ibv_scq` and `ibv_rcq` are created separately; the DEVX path also uses independent CQs.
4. **V100 MCST dump**: Before reading the receive buffer, perform a dump according to `need_mcst` to ensure GPU memory consistency.
5. **Permissions**: If `cudaHostRegisterIoMemory ... operation not permitted` occurs, use `sudo` and check the UAR mapping environment.
6. **Startup order**: Start the server first. `Unable to connect to server` usually means that the server is not yet listening.
7. **No retransmission semantics**: Packet loss, insufficient Recv WQEs, or mismatched options on the two endpoints may cause a permanent wait. UC does not recover automatically like RC.
8. **Optional host RX**: Set `SEND_RECV_UC_HOST_RX=1` to use mapped pinned host memory for the receive buffer.
9. **Library path**: If `libdoca_gpunetio_host.so.3: cannot open shared object file` occurs, set `LD_LIBRARY_PATH="$PWD/lib"`.
10. **OOB port isolation**: The default TCP port is `2000`. When running multiple test groups in parallel or avoiding a stale server, set a different `GPUNETIO_OOB_PORT` on both endpoints, for example, `GPUNETIO_OOB_PORT=2301`.
