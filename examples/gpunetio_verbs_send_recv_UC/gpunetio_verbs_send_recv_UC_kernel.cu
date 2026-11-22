/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdint.h>
#include <stdbool.h>
#define DOCA_GPUNETIO_VERBS_ENABLE_DEBUG 0
#include <doca_gpunetio_dev_verbs_twosided.cuh>
#include "gpunetio_verbs_send_recv_UC.h"

#define EXEC_MODE DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_EXCLUSIVE
#define NIC_MODE DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO
#define PROXY_OPTS DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_CPU_PROXY_UPDATE_PI

__device__ static inline uint64_t post_recv_low(struct doca_gpu_dev_verbs_qp *qp, uint8_t *rx_buf,
                                                uint32_t rx_mkey, uint32_t size) {
    uint64_t ticket = doca_gpu_dev_verbs_reserve_wq_slots<EXEC_MODE, DOCA_GPUNETIO_VERBS_QP_RQ>(
        qp, 1, PROXY_OPTS);
    struct doca_gpunetio_ib_mlx5_wqe_data_seg *rwqe =
        doca_gpu_dev_verbs_get_rwqe_ptr(qp, ticket);
    doca_gpu_dev_verbs_wqe_prepare_recv(qp, rwqe, (uint64_t)rx_buf, rx_mkey, size);
    doca_gpu_dev_verbs_mark_wqes_ready<EXEC_MODE, DOCA_GPUNETIO_VERBS_QP_RQ>(qp, ticket, ticket);
    doca_gpu_dev_verbs_submit<EXEC_MODE, DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU, NIC_MODE,
                              DOCA_GPUNETIO_VERBS_QP_RQ>(qp, ticket + 1, PROXY_OPTS);
    return ticket;
}

__device__ static inline uint64_t post_send_low(struct doca_gpu_dev_verbs_qp *qp, uint8_t *tx_buf,
                                                uint32_t tx_mkey, uint32_t size,
                                                bool use_immediate, uint32_t immediate) {
    uint64_t ticket = doca_gpu_dev_verbs_reserve_wq_slots<EXEC_MODE>(qp, 1, PROXY_OPTS);
    struct doca_gpu_dev_verbs_wqe *wqe = doca_gpu_dev_verbs_get_wqe_ptr(qp, ticket);
    uint32_t opcode = use_immediate ? DOCA_GPUNETIO_IB_MLX5_OPCODE_SEND_IMM
                                    : DOCA_GPUNETIO_IB_MLX5_OPCODE_SEND;

    if (size > 0)
        doca_gpu_dev_verbs_wqe_prepare_send(qp, wqe, ticket, opcode,
                                            DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE,
                                            doca_gpu_dev_verbs_bswap32(immediate),
                                            (uint64_t)tx_buf, tx_mkey, size);
    else
        doca_gpu_dev_verbs_wqe_prepare_send(qp, wqe, ticket, opcode,
                                            DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE,
                                            doca_gpu_dev_verbs_bswap32(immediate));

    doca_gpu_dev_verbs_mark_wqes_ready<EXEC_MODE>(qp, ticket, ticket);
    doca_gpu_dev_verbs_submit<EXEC_MODE, DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU, NIC_MODE>(
        qp, ticket + 1, PROXY_OPTS);
    return ticket;
}

__device__ static inline int poll_send_low(struct doca_gpu_dev_verbs_qp *qp, uint64_t ticket) {
    return doca_gpu_dev_verbs_poll_cq_at<EXEC_MODE>(doca_gpu_dev_verbs_qp_get_cq_sq(qp), ticket);
}

__device__ static inline int wait_recv_low(struct doca_gpu_dev_verbs_qp *qp, uint64_t ticket,
                                           uint8_t *dump_buf, uint32_t dump_mkey,
                                           struct doca_gpunetio_ib_mlx5_cqe64 **cqe) {
    int status = 0;
    if (qp->need_mcst) {
        uint64_t dump_ticket = doca_gpu_dev_verbs_reserve_wq_slots<EXEC_MODE>(qp, 1, PROXY_OPTS);
        struct doca_gpu_dev_verbs_wqe *dump_wqe =
            doca_gpu_dev_verbs_get_wqe_ptr(qp, dump_ticket);
        doca_gpu_dev_verbs_wqe_prepare_dump(qp, dump_wqe, dump_ticket,
                                            DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE,
                                            (uint64_t)dump_buf, dump_mkey, 1);
        doca_gpu_dev_verbs_mark_wqes_ready<EXEC_MODE>(qp, dump_ticket, dump_ticket);
        doca_gpu_dev_verbs_submit<EXEC_MODE, DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU, NIC_MODE>(
            qp, dump_ticket + 1, PROXY_OPTS);
        status = poll_send_low(qp, dump_ticket);
    }

    int recv_status = doca_gpu_dev_verbs_poll_cq<EXEC_MODE, DOCA_GPUNETIO_VERBS_QP_RQ>(
        doca_gpu_dev_verbs_qp_get_cq_rq(qp), ticket, cqe);
    return status != 0 ? status : recv_status;
}

__device__ static inline uint32_t validate_recv(struct doca_gpunetio_ib_mlx5_cqe64 *cqe,
                                                int poll_status, uint8_t *rx_buf, uint32_t size,
                                                bool check_immediate, uint32_t expected_imm,
                                                uint8_t expected_byte) {
    if (poll_status != 0 || cqe == nullptr) {
        printf("validate_recv: poll_status=%d cqe=%p\n", poll_status, cqe);
        return 1;
    }

    uint8_t opcode = cqe->op_own >> DOCA_GPUNETIO_VERBS_MLX5_CQE_OPCODE_SHIFT;
    if (opcode == DOCA_GPUNETIO_IB_MLX5_CQE_RESP_ERR) {
        printf("validate_recv: RESP_ERR op_own=%02x\n", cqe->op_own);
        return 1;
    }
    uint32_t actual_bytes = doca_gpu_dev_verbs_cqe_get_bytes(cqe);
    if (actual_bytes != size) {
        printf("validate_recv: byte_cnt=%u expected=%u op_own=%02x\n", actual_bytes, size,
               cqe->op_own);
        return 1;
    }
    if (check_immediate && doca_gpu_dev_verbs_cqe_get_imm(cqe) != expected_imm) {
        printf("validate_recv: imm=%08x expected=%08x\n", doca_gpu_dev_verbs_cqe_get_imm(cqe),
               expected_imm);
        return 1;
    }

    for (uint32_t i = 0; i < size; ++i) {
        if (rx_buf[i] != expected_byte) {
            printf("validate_recv: rx[%u]=%02x expected=%02x\n", i, rx_buf[i], expected_byte);
            return 1;
        }
    }
    return 0;
}

__global__ void prepare_recv_high_kernel(struct doca_gpu_dev_verbs_qp *qp, uint32_t num_recvs,
                                         uint32_t size, uint8_t *rx_buf, uint32_t rx_mkey) {
    struct doca_gpu_dev_verbs_addr rx = {(uint64_t)rx_buf, rx_mkey};
    for (uint32_t i = 0; i < num_recvs; ++i) {
        doca_gpu_dev_verbs_ticket_t ticket;
        doca_gpu_dev_verbs_recv<EXEC_MODE>(qp, rx, size, &ticket, PROXY_OPTS);
    }
}

__global__ void prepare_recv_low_kernel(struct doca_gpu_dev_verbs_qp *qp, uint32_t num_recvs,
                                        uint32_t size, uint8_t *rx_buf, uint32_t rx_mkey) {
    for (uint32_t i = 0; i < num_recvs; ++i) (void)post_recv_low(qp, rx_buf, rx_mkey, size);
}

__global__ void send_recv_high_kernel(struct doca_gpu_dev_verbs_qp *qp, uint32_t num_iters,
                                      uint32_t size, bool is_client, uint8_t *tx_buf,
                                      uint32_t tx_mkey, uint8_t *rx_buf, uint32_t rx_mkey,
                                      uint8_t *dump_buf, uint32_t dump_mkey,
                                      uint8_t expected_byte, uint32_t *error_count) {
    struct doca_gpu_dev_verbs_addr tx = {(uint64_t)tx_buf, tx_mkey};
    struct doca_gpu_dev_verbs_addr dump = {(uint64_t)dump_buf, dump_mkey};
    uint32_t errors = 0;

    for (uint32_t iter = 0; iter < num_iters; ++iter) {
        doca_gpu_dev_verbs_ticket_t recv_ticket = iter;
        doca_gpu_dev_verbs_ticket_t send_ticket = 0;
        struct doca_gpunetio_ib_mlx5_cqe64 *cqe = nullptr;

        if (is_client) {
            doca_gpu_dev_verbs_send<EXEC_MODE>(qp, tx, size, &send_ticket, PROXY_OPTS);
            errors += (poll_send_low(qp, send_ticket) != 0);
        }

        if (qp->need_mcst)
            doca_gpu_dev_verbs_recv_wait<EXEC_MODE, NIC_MODE, DOCA_GPUNETIO_VERBS_MCST_ENABLED>(
                qp, dump, &recv_ticket, &cqe);
        else
            doca_gpu_dev_verbs_recv_wait<EXEC_MODE, NIC_MODE, DOCA_GPUNETIO_VERBS_MCST_DISABLED>(
                qp, dump, &recv_ticket, &cqe);

        errors += validate_recv(cqe, 0, rx_buf, size, false, 0, expected_byte);

        if (!is_client) {
            doca_gpu_dev_verbs_send<EXEC_MODE>(qp, tx, size, &send_ticket, PROXY_OPTS);
            errors += (poll_send_low(qp, send_ticket) != 0);
        }
    }

    *error_count = errors;
}

__global__ void send_recv_low_kernel(struct doca_gpu_dev_verbs_qp *qp, uint32_t num_iters,
                                     uint32_t size, bool use_immediate, uint32_t expected_imm,
                                     bool is_client, uint8_t *tx_buf, uint32_t tx_mkey,
                                     uint8_t *rx_buf, uint32_t rx_mkey, uint8_t *dump_buf,
                                     uint32_t dump_mkey, uint8_t expected_byte,
                                     uint32_t *error_count) {
    uint32_t errors = 0;

    for (uint32_t iter = 0; iter < num_iters; ++iter) {
        uint64_t recv_ticket = iter;
        struct doca_gpunetio_ib_mlx5_cqe64 *cqe = nullptr;

        if (is_client) {
            uint64_t send_ticket =
                post_send_low(qp, tx_buf, tx_mkey, size, use_immediate, expected_imm);
            errors += (poll_send_low(qp, send_ticket) != 0);
        }

        int recv_status = wait_recv_low(qp, recv_ticket, dump_buf, dump_mkey, &cqe);
        errors += validate_recv(cqe, recv_status, rx_buf, size, use_immediate, expected_imm,
                                expected_byte);

        if (!is_client) {
            uint64_t send_ticket =
                post_send_low(qp, tx_buf, tx_mkey, size, use_immediate, expected_imm);
            errors += (poll_send_low(qp, send_ticket) != 0);
        }
    }

    *error_count = errors;
}

extern "C" doca_error_t gpunetio_verbs_send_recv_uc_prepare(
    cudaStream_t stream, struct doca_gpu_dev_verbs_qp *qp, uint32_t num_recvs, uint32_t size,
    bool use_low_level, uint8_t *rx_buf, uint32_t rx_mkey) {
    if (use_low_level)
        prepare_recv_low_kernel<<<1, 1, 0, stream>>>(qp, num_recvs, size, rx_buf, rx_mkey);
    else
        prepare_recv_high_kernel<<<1, 1, 0, stream>>>(qp, num_recvs, size, rx_buf, rx_mkey);

    cudaError_t result = cudaGetLastError();
    return result == cudaSuccess ? DOCA_SUCCESS : DOCA_ERROR_DRIVER;
}

extern "C" doca_error_t gpunetio_verbs_send_recv_uc(
    cudaStream_t stream, struct doca_gpu_dev_verbs_qp *qp, uint32_t num_iters, uint32_t size,
    bool use_low_level, bool use_immediate, bool is_client, uint8_t *tx_buf, uint32_t tx_mkey,
    uint8_t *rx_buf, uint32_t rx_mkey, uint8_t *dump_buf, uint32_t dump_mkey,
    uint8_t expected_byte, uint32_t expected_imm, uint32_t *error_count) {
    if (use_low_level)
        send_recv_low_kernel<<<1, 1, 0, stream>>>(qp, num_iters, size, use_immediate, expected_imm,
                                                  is_client, tx_buf, tx_mkey, rx_buf, rx_mkey,
                                                  dump_buf, dump_mkey, expected_byte, error_count);
    else
        send_recv_high_kernel<<<1, 1, 0, stream>>>(qp, num_iters, size, is_client, tx_buf, tx_mkey,
                                                   rx_buf, rx_mkey, dump_buf, dump_mkey,
                                                   expected_byte, error_count);

    cudaError_t result = cudaGetLastError();
    return result == cudaSuccess ? DOCA_SUCCESS : DOCA_ERROR_DRIVER;
}
