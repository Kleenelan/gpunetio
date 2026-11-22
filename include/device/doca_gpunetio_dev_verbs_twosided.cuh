/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file doca_gpunetio_dev_verbs_twosided.cuh
 * @brief GDAKI CUDA device functions for two-sided RC Send/Receive operations.
 */
#ifndef DOCA_GPUNETIO_DEV_VERBS_TWOSIDED_CUH
#define DOCA_GPUNETIO_DEV_VERBS_TWOSIDED_CUH

#include "doca_gpunetio_dev_verbs_qp.cuh"
#include "doca_gpunetio_dev_verbs_cq.cuh"

/* **************************************** SEND **************************************** */

/** Post one or more Send WQEs from the calling CUDA thread. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO>
__device__ static __forceinline__ void doca_gpu_dev_verbs_send_thread(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    doca_gpu_dev_verbs_ticket_t *out_ticket,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
    struct doca_gpu_dev_verbs_wqe *wqe_ptr;
    uint64_t wqe_idx;
    size_t remaining_size = size;
    uint32_t num_chunks = doca_gpu_dev_verbs_div_ceil_aligned_pow2_32bits(
        size, DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE_SHIFT);
    num_chunks = num_chunks > 0 ? num_chunks : 1;

    DOCA_GPUNETIO_VERBS_ASSERT(out_ticket != NULL);
    DOCA_GPUNETIO_VERBS_ASSERT(qp != NULL);

    uint64_t base_wqe_idx =
        doca_gpu_dev_verbs_reserve_wq_slots<resource_sharing_mode>(qp, num_chunks, code_opt);
    wqe_idx = base_wqe_idx;

#pragma unroll 1
    for (uint64_t i = 0; i < num_chunks; i++) {
        wqe_idx = base_wqe_idx + i;
        size_t chunk_size = remaining_size > DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE
                                ? DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE
                                : remaining_size;
        wqe_ptr = doca_gpu_dev_verbs_get_wqe_ptr(qp, wqe_idx);

        if (chunk_size > 0)
            doca_gpu_dev_verbs_wqe_prepare_send(
                qp, wqe_ptr, wqe_idx, DOCA_GPUNETIO_IB_MLX5_OPCODE_SEND,
                DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE, 0,
                laddr.addr + (i * DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE), laddr.key, chunk_size);
        else
            doca_gpu_dev_verbs_wqe_prepare_send(qp, wqe_ptr, wqe_idx,
                                                DOCA_GPUNETIO_IB_MLX5_OPCODE_SEND,
                                                DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE, 0);
        remaining_size -= chunk_size;
    }

    doca_gpu_dev_verbs_mark_wqes_ready<resource_sharing_mode>(qp, base_wqe_idx, wqe_idx);
    constexpr enum doca_gpu_dev_verbs_sync_scope submit_sync_scope =
        (resource_sharing_mode == DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU)
            ? DOCA_GPUNETIO_VERBS_SYNC_SCOPE_THREAD
            : DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU;
    doca_gpu_dev_verbs_submit<resource_sharing_mode, submit_sync_scope, nic_handler>(
        qp, wqe_idx + 1, code_opt);
    *out_ticket = wqe_idx;
}

/** Post one Send WQE per lane from a full CUDA warp. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO>
__device__ static __forceinline__ void doca_gpu_dev_verbs_send_warp(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    doca_gpu_dev_verbs_ticket_t *out_ticket,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
#if __CUDA_ARCH__ >= 800
    struct doca_gpu_dev_verbs_wqe *wqe_ptr;
    uint64_t base_wqe_idx = 0;
    uint32_t base_wqe_idx_0 = 0, base_wqe_idx_1 = 0;
    uint32_t lane_idx = doca_gpu_dev_verbs_get_lane_id();

    DOCA_GPUNETIO_VERBS_ASSERT(size <= DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE);
    DOCA_GPUNETIO_VERBS_ASSERT(out_ticket != NULL);
    DOCA_GPUNETIO_VERBS_ASSERT(qp != NULL);

    if (lane_idx == 0) {
        base_wqe_idx = doca_gpu_dev_verbs_reserve_wq_slots<resource_sharing_mode>(
            qp, DOCA_GPUNETIO_VERBS_WARP_SIZE, code_opt);
        base_wqe_idx_0 = (uint32_t)base_wqe_idx;
        base_wqe_idx_1 = (uint32_t)(base_wqe_idx >> 32);
    }
    __syncwarp();
    base_wqe_idx_0 = __reduce_max_sync(DOCA_GPUNETIO_VERBS_WARP_FULL_MASK, base_wqe_idx_0);
    base_wqe_idx_1 = __reduce_max_sync(DOCA_GPUNETIO_VERBS_WARP_FULL_MASK, base_wqe_idx_1);
    base_wqe_idx = ((uint64_t)base_wqe_idx_1) << 32 | base_wqe_idx_0;

    uint64_t wqe_idx = base_wqe_idx + lane_idx;
    wqe_ptr = doca_gpu_dev_verbs_get_wqe_ptr(qp, wqe_idx);
    if (size > 0)
        doca_gpu_dev_verbs_wqe_prepare_send(qp, wqe_ptr, wqe_idx,
                                            DOCA_GPUNETIO_IB_MLX5_OPCODE_SEND,
                                            DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE, 0,
                                            laddr.addr, laddr.key, size);
    else
        doca_gpu_dev_verbs_wqe_prepare_send(qp, wqe_ptr, wqe_idx,
                                            DOCA_GPUNETIO_IB_MLX5_OPCODE_SEND,
                                            DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE, 0);
    __syncwarp();

    if (lane_idx == 0) {
        doca_gpu_dev_verbs_mark_wqes_ready<resource_sharing_mode>(
            qp, base_wqe_idx, base_wqe_idx + DOCA_GPUNETIO_VERBS_WARP_SIZE - 1);
        constexpr enum doca_gpu_dev_verbs_sync_scope submit_sync_scope =
            (resource_sharing_mode == DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU)
                ? DOCA_GPUNETIO_VERBS_SYNC_SCOPE_THREAD
                : DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU;
        doca_gpu_dev_verbs_submit<resource_sharing_mode, submit_sync_scope, nic_handler>(
            qp, base_wqe_idx + DOCA_GPUNETIO_VERBS_WARP_SIZE, code_opt);
    }
    __syncwarp();
    *out_ticket = wqe_idx;
#else
    (void)qp;
    (void)laddr;
    (void)size;
    (void)code_opt;
    *out_ticket = 0;
#endif
}

/** Two-sided Send dispatcher. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_exec_scope exec_scope = DOCA_GPUNETIO_VERBS_EXEC_SCOPE_THREAD>
__device__ static __forceinline__ void doca_gpu_dev_verbs_send(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    doca_gpu_dev_verbs_ticket_t *out_ticket,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
    if (exec_scope == DOCA_GPUNETIO_VERBS_EXEC_SCOPE_THREAD)
        doca_gpu_dev_verbs_send_thread<resource_sharing_mode, nic_handler>(qp, laddr, size,
                                                                            out_ticket, code_opt);
    if (exec_scope == DOCA_GPUNETIO_VERBS_EXEC_SCOPE_WARP)
        doca_gpu_dev_verbs_send_warp<resource_sharing_mode, nic_handler>(qp, laddr, size, out_ticket,
                                                                          code_opt);
}

template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_exec_scope exec_scope = DOCA_GPUNETIO_VERBS_EXEC_SCOPE_THREAD>
__device__ static __forceinline__ void doca_gpu_dev_verbs_send(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
    doca_gpu_dev_verbs_ticket_t ticket;
    doca_gpu_dev_verbs_send<resource_sharing_mode, nic_handler, exec_scope>(qp, laddr, size, &ticket,
                                                                             code_opt);
}

/* **************************************** RECV **************************************** */

/** Post one receive WQE from the calling CUDA thread. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO>
__device__ static __forceinline__ void doca_gpu_dev_verbs_recv_thread(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    doca_gpu_dev_verbs_ticket_t *out_ticket,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
    DOCA_GPUNETIO_VERBS_ASSERT(out_ticket != NULL);
    DOCA_GPUNETIO_VERBS_ASSERT(qp != NULL);
    DOCA_GPUNETIO_VERBS_ASSERT(size <= DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE);

    uint64_t rwqe_idx = doca_gpu_dev_verbs_reserve_wq_slots<resource_sharing_mode,
                                                            DOCA_GPUNETIO_VERBS_QP_RQ>(qp, 1,
                                                                                       code_opt);
    struct doca_gpunetio_ib_mlx5_wqe_data_seg *rwqe_ptr =
        doca_gpu_dev_verbs_get_rwqe_ptr(qp, rwqe_idx);
    doca_gpu_dev_verbs_wqe_prepare_recv(qp, rwqe_ptr, laddr.addr, laddr.key, size);
    doca_gpu_dev_verbs_mark_wqes_ready<resource_sharing_mode, DOCA_GPUNETIO_VERBS_QP_RQ>(
        qp, rwqe_idx, rwqe_idx);

    constexpr enum doca_gpu_dev_verbs_sync_scope submit_sync_scope =
        (resource_sharing_mode == DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU)
            ? DOCA_GPUNETIO_VERBS_SYNC_SCOPE_THREAD
            : DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU;
    doca_gpu_dev_verbs_submit<resource_sharing_mode, submit_sync_scope, nic_handler,
                              DOCA_GPUNETIO_VERBS_QP_RQ>(qp, rwqe_idx + 1, code_opt);
    qp->last_recv_wqe_idx = rwqe_idx;
    *out_ticket = rwqe_idx;
}

/** Post one receive WQE per lane from a full CUDA warp. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO>
__device__ static __forceinline__ void doca_gpu_dev_verbs_recv_warp(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    doca_gpu_dev_verbs_ticket_t *out_ticket,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
#if __CUDA_ARCH__ >= 800
    uint64_t base_wqe_idx = 0;
    uint32_t base_wqe_idx_0 = 0, base_wqe_idx_1 = 0;
    uint32_t lane_idx = doca_gpu_dev_verbs_get_lane_id();

    DOCA_GPUNETIO_VERBS_ASSERT(size <= DOCA_GPUNETIO_VERBS_MAX_TRANSFER_SIZE);
    DOCA_GPUNETIO_VERBS_ASSERT(out_ticket != NULL);
    DOCA_GPUNETIO_VERBS_ASSERT(qp != NULL);

    if (lane_idx == 0) {
        base_wqe_idx = doca_gpu_dev_verbs_reserve_wq_slots<resource_sharing_mode,
                                                           DOCA_GPUNETIO_VERBS_QP_RQ>(
            qp, DOCA_GPUNETIO_VERBS_WARP_SIZE, code_opt);
        base_wqe_idx_0 = (uint32_t)base_wqe_idx;
        base_wqe_idx_1 = (uint32_t)(base_wqe_idx >> 32);
    }
    __syncwarp();
    base_wqe_idx_0 = __reduce_max_sync(DOCA_GPUNETIO_VERBS_WARP_FULL_MASK, base_wqe_idx_0);
    base_wqe_idx_1 = __reduce_max_sync(DOCA_GPUNETIO_VERBS_WARP_FULL_MASK, base_wqe_idx_1);
    base_wqe_idx = ((uint64_t)base_wqe_idx_1) << 32 | base_wqe_idx_0;

    uint64_t rwqe_idx = base_wqe_idx + lane_idx;
    struct doca_gpunetio_ib_mlx5_wqe_data_seg *rwqe_ptr =
        doca_gpu_dev_verbs_get_rwqe_ptr(qp, rwqe_idx);
    doca_gpu_dev_verbs_wqe_prepare_recv(qp, rwqe_ptr, laddr.addr, laddr.key, size);
    __syncwarp();

    if (lane_idx == 0) {
        doca_gpu_dev_verbs_mark_wqes_ready<resource_sharing_mode, DOCA_GPUNETIO_VERBS_QP_RQ>(
            qp, base_wqe_idx, base_wqe_idx + DOCA_GPUNETIO_VERBS_WARP_SIZE - 1);
        constexpr enum doca_gpu_dev_verbs_sync_scope submit_sync_scope =
            (resource_sharing_mode == DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU)
                ? DOCA_GPUNETIO_VERBS_SYNC_SCOPE_THREAD
                : DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU;
        doca_gpu_dev_verbs_submit<resource_sharing_mode, submit_sync_scope, nic_handler,
                                  DOCA_GPUNETIO_VERBS_QP_RQ>(
            qp, base_wqe_idx + DOCA_GPUNETIO_VERBS_WARP_SIZE, code_opt);
        qp->last_recv_wqe_idx = base_wqe_idx + DOCA_GPUNETIO_VERBS_WARP_SIZE - 1;
    }
    __syncwarp();
    *out_ticket = rwqe_idx;
#else
    (void)qp;
    (void)laddr;
    (void)size;
    (void)code_opt;
    *out_ticket = 0;
#endif
}

/** Two-sided Receive dispatcher. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_exec_scope exec_scope = DOCA_GPUNETIO_VERBS_EXEC_SCOPE_THREAD>
__device__ static __forceinline__ void doca_gpu_dev_verbs_recv(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    doca_gpu_dev_verbs_ticket_t *out_ticket,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
    if (exec_scope == DOCA_GPUNETIO_VERBS_EXEC_SCOPE_THREAD)
        doca_gpu_dev_verbs_recv_thread<resource_sharing_mode, nic_handler>(qp, laddr, size,
                                                                            out_ticket, code_opt);
    if (exec_scope == DOCA_GPUNETIO_VERBS_EXEC_SCOPE_WARP)
        doca_gpu_dev_verbs_recv_warp<resource_sharing_mode, nic_handler>(qp, laddr, size, out_ticket,
                                                                          code_opt);
}

template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_exec_scope exec_scope = DOCA_GPUNETIO_VERBS_EXEC_SCOPE_THREAD>
__device__ static __forceinline__ void doca_gpu_dev_verbs_recv(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr laddr, size_t size,
    uint32_t code_opt = DOCA_GPUNETIO_VERBS_GPU_CODE_OPT_DEFAULT) {
    doca_gpu_dev_verbs_ticket_t ticket;
    doca_gpu_dev_verbs_recv<resource_sharing_mode, nic_handler, exec_scope>(qp, laddr, size, &ticket,
                                                                             code_opt);
}

/* **************************************** RECV WAIT **************************************** */

template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_mcst_mode mcst_mode = DOCA_GPUNETIO_VERBS_MCST_DISABLED,
          enum doca_gpu_dev_verbs_blocking_mode blocking_mode =
              DOCA_GPUNETIO_VERBS_BLOCKING_MODE_ENABLED>
__device__ static __forceinline__ void doca_priv_gpu_dev_verbs_recv_wait(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr daddr,
    doca_gpu_dev_verbs_ticket_t *ticket, struct doca_gpunetio_ib_mlx5_cqe64 **cqe64) {
    if (mcst_mode == DOCA_GPUNETIO_VERBS_MCST_ENABLED) {
        uint64_t wqe_idx = doca_gpu_dev_verbs_reserve_wq_slots<resource_sharing_mode>(qp, 1);
        struct doca_gpu_dev_verbs_wqe *wqe_ptr = doca_gpu_dev_verbs_get_wqe_ptr(qp, wqe_idx);
        doca_gpu_dev_verbs_wqe_prepare_dump(qp, wqe_ptr, wqe_idx,
                                            DOCA_GPUNETIO_IB_MLX5_WQE_CTRL_CQ_UPDATE,
                                            daddr.addr, daddr.key, 1);
        doca_gpu_dev_verbs_mark_wqes_ready<resource_sharing_mode>(qp, wqe_idx, wqe_idx);
        doca_gpu_dev_verbs_submit<resource_sharing_mode, DOCA_GPUNETIO_VERBS_SYNC_SCOPE_GPU,
                                  nic_handler>(qp, wqe_idx + 1);
        qp->last_dump_wqe_idx = wqe_idx;
        doca_gpu_dev_verbs_poll_cq_at<resource_sharing_mode>(doca_gpu_dev_verbs_qp_get_cq_sq(qp),
                                                              wqe_idx);
    }
    doca_gpu_dev_verbs_poll_cq<resource_sharing_mode, DOCA_GPUNETIO_VERBS_QP_RQ, blocking_mode>(
        doca_gpu_dev_verbs_qp_get_cq_rq(qp), *ticket, cqe64);
}

/** Wait for the receive WQE identified by ticket and optionally return its CQE. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_mcst_mode mcst_mode = DOCA_GPUNETIO_VERBS_MCST_DISABLED,
          enum doca_gpu_dev_verbs_blocking_mode blocking_mode =
              DOCA_GPUNETIO_VERBS_BLOCKING_MODE_ENABLED>
__device__ static __forceinline__ void doca_gpu_dev_verbs_recv_wait(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr daddr,
    doca_gpu_dev_verbs_ticket_t *ticket, struct doca_gpunetio_ib_mlx5_cqe64 **cqe64) {
    doca_priv_gpu_dev_verbs_recv_wait<resource_sharing_mode, nic_handler, mcst_mode, blocking_mode>(
        qp, daddr, ticket, cqe64);
}

/** Wait for the most recently reserved receive WQE. */
template <enum doca_gpu_dev_verbs_resource_sharing_mode resource_sharing_mode =
              DOCA_GPUNETIO_VERBS_RESOURCE_SHARING_MODE_GPU,
          enum doca_gpu_dev_verbs_nic_handler nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO,
          enum doca_gpu_dev_verbs_mcst_mode mcst_mode = DOCA_GPUNETIO_VERBS_MCST_DISABLED,
          enum doca_gpu_dev_verbs_blocking_mode blocking_mode =
              DOCA_GPUNETIO_VERBS_BLOCKING_MODE_ENABLED>
__device__ static __forceinline__ void doca_gpu_dev_verbs_recv_wait(
    struct doca_gpu_dev_verbs_qp *qp, struct doca_gpu_dev_verbs_addr daddr,
    struct doca_gpunetio_ib_mlx5_cqe64 **cqe64) {
    uint64_t ticket = doca_gpu_dev_verbs_atomic_read<uint64_t, resource_sharing_mode>(
        &qp->rq_rsvd_index);
    [[unlikely]] if (ticket == 0)
        return;
    --ticket;
    doca_priv_gpu_dev_verbs_recv_wait<resource_sharing_mode, nic_handler, mcst_mode, blocking_mode>(
        qp, daddr, &ticket, cqe64);
}

#endif /* DOCA_GPUNETIO_DEV_VERBS_TWOSIDED_CUH */
