# RC Send/Receive Example with GPU-Direct Doorbells: Build and Run Guide

This document describes the RC Send/Receive example added to this repository:

```text
examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv
```

The example covers:

- High-level APIs: `doca_gpu_dev_verbs_send/recv/recv_wait`
- Low-level APIs: RQ/SQ WQE reservation, construction, submission, and CQE polling
- `SEND` and `SEND_IMM`
- Ringing NIC doorbells directly from the GPU (`GPU_SM_DB`)
- CPU-assisted doorbells (`CPU_PROXY`)
- Standard `ibv_qp` and DEVX backends

---

## 1. Environment and Dependencies

Validated local environment:

- GPU: Tesla V100-PCIE-32GB, `sm_70`, PCIe address `65:00.0`
- NIC: ConnectX-6 Dx with two RoCE ports directly connected by optical fiber
  - `mlx5_0`: `192.168.0.200`
  - `mlx5_1`: `192.168.0.201`
- CUDA: 12.2
- rdma-core: `libibverbs` and `libmlx5`
- Run with `sudo` to satisfy UAR MMIO registration permission requirements

`cudaHostRegisterIoMemory` must succeed when the GPU rings doorbells directly. Regular users may encounter `operation not permitted`, so using `sudo` is recommended.

---

## 2. Build

Run the following commands from the repository root:

```bash
# Clean stale objects if needed to avoid reusing them after header changes
make clean

# Build the host library first, then build all examples
make -j4 lib CUDA_ARCH=70
make -j4 examples CUDA_ARCH=70
```

To build only this example:

```bash
make -C examples/gpunetio_verbs_send_recv \
    TOP_DIR=$PWD CUDA_HOME=/usr/local/cuda CUDA_ARCH=70 -j4
```

The resulting executable is:

```text
examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv
```

---

## 3. Basic Usage

The example uses a client/server architecture. Start the server before the client.

### 3.1 Set Up the Environment

```bash
cd /home/hongleili/ex_doca/tmp03_gpunetio_UC_send_receive_impl/gpunetio
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"
export DOCA_GPUNETIO_LOG=6
```

To reduce log output:

```bash
export DOCA_GPUNETIO_LOG=3
```

### 3.2 Server: `mlx5_0`

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv
```

### 3.3 Client: `mlx5_1`

After starting the server, run:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

`192.168.0.200` is the server's OOB address. When running on two hosts, replace it with the server host's actual OOB IP address.

On success, each endpoint prints a message similar to:

```text
Send/Receive validation successful: high-level API, SEND, 2 iterations x 64 bytes
```

---

## 4. Command-Line Options

| Option | Description |
|--------|-------------|
| `-c <server_ip>` | Run in client mode and connect to the server's OOB address |
| `-d <mlx5_X>` | RDMA device name, such as `mlx5_0` or `mlx5_1` |
| `-g <GPU PCIe>` | GPU PCIe address; `65:00.0` on this machine |
| `-l <GID index>` | GID index; RoCEv2 uses `3` on this machine |
| `-p <handler>` | `0: AUTO`, `1: CPU_PROXY`, `2: GPU_SM_DB` |
| `-a <high|low>` | High-level or low-level GPU API; default: `high` |
| `-b <ibv|devx>` | QP backend; default: `ibv` |
| `-m` | Use `SEND_IMM`; only valid together with `-a low` |
| `-s <bytes>` | Message size; default: `64`; `0` is supported |
| `-i <iterations>` | Number of ping-pong iterations; default: `4`; maximum: `128` |
| `-r <mode>` | Send DBR mode; default: `0` |

Note: `-m` can only be used with `-a low`.

---

## 5. Recommended Test Matrix

### 5.1 High-Level API + GPU-Direct Doorbells + SEND

Server:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv
```

Client:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a high -b ibv \
    -c 192.168.0.200
```

### 5.2 Low-Level API + GPU-Direct Doorbells + SEND_IMM

Server:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_0 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a low -m -b ibv
```

Client:

```bash
sudo -S env LD_LIBRARY_PATH="$PWD/lib" DOCA_GPUNETIO_LOG=6 \
    examples/gpunetio_verbs_send_recv/gpunetio_verbs_send_recv \
    -d mlx5_1 -g 65:00.0 -l 3 -p 2 -i 2 -s 64 -a low -m -b ibv \
    -c 192.168.0.200
```

### 5.3 High-Level API + CPU Proxy + SEND

Replace `-p 2` with `-p 1`:

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

### 5.4 Low-Level API + CPU Proxy + SEND_IMM

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

### 5.5 Zero-Length SEND

Replace `-s 64` with `-s 0` to cover the zero-byte send path.

---

## 6. APIs Used by the Example

### High-Level Path (`-a high`)

- `doca_gpu_dev_verbs_recv`
- `doca_gpu_dev_verbs_send`
- `doca_gpu_dev_verbs_recv_wait`
- `doca_gpu_dev_verbs_cqe_get_bytes`
- `doca_gpu_dev_verbs_cqe_get_imm`

### Low-Level Path (`-a low`)

- `doca_gpu_dev_verbs_reserve_wq_slots`
- `doca_gpu_dev_verbs_get_wqe_ptr`
- `doca_gpu_dev_verbs_get_rwqe_ptr`
- `doca_gpu_dev_verbs_wqe_prepare_send`
- `doca_gpu_dev_verbs_wqe_prepare_recv`
- `doca_gpu_dev_verbs_mark_wqes_ready`
- `doca_gpu_dev_verbs_submit`
- `doca_gpu_dev_verbs_poll_cq`
- `doca_gpu_dev_verbs_poll_cq_at`

The example validates both the `SEND_IMM` immediate value and the immediate value in the receive CQE.

---

## 7. Backend Selection

### `-b ibv` (Recommended)

Create an RC QP with standard `ibv_create_qp`, then export it to the GPU with:

```c
doca_gpu_verbs_ibv_export_qp()
```

The following combinations have been validated on this machine:

- `high + SEND + GPU_SM_DB`
- `low + SEND_IMM + GPU_SM_DB`
- `high + SEND + CPU_PROXY`
- `low + SEND_IMM + CPU_PROXY`

### `-b devx`

Use the repository's existing custom DEVX QP creation path. This backend has also been validated locally with direct/proxy and high/low modes. The default example backend is `ibv`; specify `-b devx` explicitly to validate custom DEVX.

---

## 8. Notes for This Machine

1. **Separate SQ/RQ CQs are required**: Create `ibv_scq` and `ibv_rcq` separately to avoid mixing SEND, RECV, and MCST dump CQEs.
2. **V100 requires an MCST dump**: Before reading the receive buffer, the example performs a dump according to `need_mcst` to ensure GPU memory consistency.
3. **sudo is used for UAR mapping**: GPU-direct doorbells require MMIO mapping permission.
4. **GDRCopy is not required for this example**: It is only needed by other `GPU_CPU` or no-DBR features. When using those features, ensure that `/dev/gdrdrv` exists.
5. **OOB startup order**: Always start the server before the client. Starting the client too early causes `Unable to connect to server`.
6. **Stale objects**: Run `make clean` after modifying public headers to prevent different compilation units from using different structure layouts.

---

## 9. Troubleshooting

### `cudaHostRegister ... operation not permitted`

Run with `sudo` and ensure that the system permits UAR MMIO mapping when using `-p 2`.

### `Unable to connect to server`

The server has not finished initializing and started listening. Increase the client startup delay, or first confirm that the server log contains:

```text
Listening for incoming connections
```

### `Send/Receive validation failed`

Check the following:

- `-a`, `-b`, `-m`, `-s`, and `-i` match on both endpoints;
- The GID index is correct (`3` on this machine);
- Separate SQ/RQ CQs are being used;
- The programs are running with `sudo`;
- Whether an actual CQE error occurred. Set `DOCA_GPUNETIO_LOG=6` for detailed logs.

### `libdoca_gpunetio_host.so.3: cannot open shared object file`

Check the library path:

```bash
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"
```
