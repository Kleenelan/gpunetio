/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file doca_gpunetio_dev_verbs_dev.h
 * @brief GDAKI common definitions
 *
 * @{
 */
#ifndef DOCA_GPUNETIO_VERBS_DEV_H
#define DOCA_GPUNETIO_VERBS_DEV_H

#include "doca_gpunetio_verbs_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef doca_gpu_dev_verbs_ticket_t
 * @brief Ticket type used in one-sided APIs.
 */
typedef uint64_t doca_gpu_dev_verbs_ticket_t;

/**
 * @struct doca_gpu_dev_verbs_addr
 * @brief This structure holds the address and key of a memory region.
 */
struct doca_gpu_dev_verbs_addr {
    uint64_t addr;
    __be32 key;
};

typedef struct {
    uint32_t add_data;
    uint32_t field_boundary;
    uint64_t reserved;
} __attribute__((__packed__)) doca_gpu_dev_verbs_atomic_32_masked_fa_seg_t;
#if __cplusplus >= 201103L
static_assert(sizeof(doca_gpu_dev_verbs_atomic_32_masked_fa_seg_t) == 16,
              "sizeof(doca_gpu_dev_verbs_atomic_32_masked_fa_seg_t) == 16 failed.");
#endif

typedef struct {
    uint64_t add_data;
    uint64_t field_boundary;
} __attribute__((__packed__)) doca_gpu_dev_verbs_atomic_64_masked_fa_seg_t;
#if __cplusplus >= 201103L
static_assert(sizeof(doca_gpu_dev_verbs_atomic_64_masked_fa_seg_t) == 16,
              "sizeof(doca_gpu_dev_verbs_atomic_64_masked_fa_seg_t) == 16 failed.");
#endif

typedef struct {
    uint32_t swap_data;
    uint32_t compare_data;
    uint32_t swap_mask;
    uint32_t compare_mask;
} __attribute__((__packed__)) doca_gpu_dev_verbs_atomic_32_masked_cs_seg_t;
#if __cplusplus >= 201103L
static_assert(sizeof(doca_gpu_dev_verbs_atomic_32_masked_cs_seg_t) == 16,
              "sizeof(doca_gpu_dev_verbs_atomic_32_masked_cs_seg_t) == 16 failed.");
#endif

typedef struct {
    uint64_t swap;
    uint64_t compare;
} __attribute__((__packed__)) doca_gpu_dev_verbs_atomic_64_masked_cs_seg_t;
#if __cplusplus >= 201103L
static_assert(sizeof(doca_gpu_dev_verbs_atomic_64_masked_cs_seg_t) == 16,
              "sizeof(doca_gpu_dev_verbs_atomic_64_masked_cs_seg_t) == 16 failed.");
#endif

/**
 * Describes GPUNetIO dev general WQE.
 */
struct doca_gpu_dev_verbs_wqe {
    union {
        /* Generic inline Data */
        struct {
            uint8_t inl_data[64];
        };

        /* Generic Data */
        struct {
            struct doca_gpunetio_ib_mlx5_wqe_data_seg dseg0;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg dseg1;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg dseg2;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg dseg3;
        };

        /* Read/Write */
        struct {
            struct doca_gpunetio_ib_mlx5_wqe_ctrl_seg rw_cseg;
            struct doca_gpunetio_ib_mlx5_wqe_raddr_seg rw_rseg;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg rw_dseg0;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg rw_dseg1;
        };

        /* Atomic */
        struct {
            struct doca_gpunetio_ib_mlx5_wqe_ctrl_seg at_cseg;
            struct doca_gpunetio_ib_mlx5_wqe_raddr_seg at_rseg;
            struct doca_gpunetio_ib_mlx5_wqe_atomic_seg at_seg;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg at_dseg;
        };

        /* Send */
        struct {
            struct doca_gpunetio_ib_mlx5_wqe_ctrl_seg snd_cseg;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg snd_dseg0;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg snd_dseg1;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg snd_dseg2;
        };

        /* Wait */
        struct {
            struct doca_gpunetio_ib_mlx5_wqe_ctrl_seg wait_cseg;
            struct doca_gpunetio_ib_mlx5_wqe_wait_seg wait_dseg;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg padding0;
            struct doca_gpunetio_ib_mlx5_wqe_data_seg padding1;
        };
    };
} __attribute__((__aligned__(8)));

/**
 * Describes a 64B CQE carrying up to 32B of inline receive data.
 */
struct doca_gpu_dev_verbs_cqe64_inline {
    uint8_t inl_data[32];
    __be32 srqn_uidx;
    __be32 imm_inval_pkey;
    uint8_t app;
    uint8_t app_op;
    __be16 app_info;
    __be32 byte_cnt;
    __be64 timestamp;
    __be32 sop_drop_qpn;
    __be16 wqe_counter;
    uint8_t signature;
    uint8_t op_own;
} __attribute__((__packed__));

#if __cplusplus >= 201103L
static_assert(sizeof(struct doca_gpu_dev_verbs_cqe64_inline) == DOCA_GPUNETIO_VERBS_CQE_SIZE,
              "Inline CQE must be 64B");
#endif

/**
 * Describes GPUNetIO dev CQ
 */
struct doca_gpu_dev_verbs_cq {
    uint8_t *cqe_daddr;                        /**< CQE address */
    uint32_t cq_num;                           /**< CQ number */
    uint32_t cqe_num;                          /**< Total number of CQEs in CQ */
    __be32 *dbrec;                             /**< CQE Doorbell Record */
    uint64_t cqe_ci;                           /**< CQE Consumer Index */
    uint32_t cqe_mask;                         /**< Mask of total number of CQEs in CQ */
    uint8_t cqe_size;                          /**< Single CQE size (64B default) */
    uint8_t reserved1[3];                      /**< Reserved */
    uint64_t cqe_rsvd;                         /**< All previous CQEs are polled */
    enum doca_gpu_dev_verbs_mem_type mem_type; /**< Memory type of the completion queue */
    uint8_t reserved2[12];                     /**< Reserved */
} __attribute__((__aligned__(8))) __attribute__((__packed__));

/**
 * Describes GPUNetIO dev QP
 */
struct doca_gpu_dev_verbs_qp {
    uint64_t sq_rsvd_index;        /**< All WQE slots prior to this index are reserved */
    uint64_t sq_ready_index;       /**< All WQE slots prior to this index are ready */
    uint64_t sq_wqe_pi;            /**< SQ WQE producer index */
    uint32_t sq_num;               /**< SQ num */
    uint32_t sq_num_shift8;        /**< SQ num << 8 */
    uint32_t sq_num_shift8_be;     /**< SQ num << 8 big endian */
    uint32_t sq_num_shift8_be_1ds; /**< SQ num << 8 big endian | 1 data segment */
    uint32_t sq_num_shift8_be_2ds; /**< SQ num << 8 big endian | 2 data segment */
    uint32_t sq_num_shift8_be_3ds; /**< SQ num << 8 big endian | 3 data segment */
    uint32_t sq_num_shift8_be_4ds; /**< SQ num << 8 big endian | 4 data segment */
    uint32_t sq_num_shift8_be_5ds; /**< SQ num << 8 big endian | 5 data segment */
    int sq_lock;                   /**< SQ lock */
    uint16_t sq_wqe_num;           /**< SQ WQE num */
    uint16_t sq_wqe_mask;          /**< SQ WQE num mask */
    uint8_t *sq_wqe_daddr;         /**< SQ WQE address */
    __be32 *sq_dbrec;              /**< SQ DBREC address */
    uint64_t *sq_db;               /**< SQ DB address */

    uint32_t rq_num;               /**< RQ number (same as SQ number for RC QPs) */
    uint32_t rcv_wqe_size;         /**< Receive WQE size in bytes */
    uint64_t rq_wqe_pi;            /**< RQ WQE producer index */
    uint32_t rq_wqe_num;           /**< RQ WQE num */
    uint32_t rq_wqe_mask;          /**< RQ WQE num mask */
    uint8_t *rq_wqe_daddr;         /**< RQ WQE address */
    __be32 *rq_dbrec;              /**< RQ DBREC address */
    uint64_t *rq_db;               /**< RQ CPU-proxy notification address */
    uint64_t rq_rsvd_index;        /**< All receive WQE slots prior to this index are reserved */
    uint64_t rq_ready_index;       /**< All receive WQE slots prior to this index are ready */
    int rq_lock;                   /**< RQ lock */
    uint8_t reserved1[4];          /**< Reserved */

    struct doca_gpu_dev_verbs_cq cq_sq; /**< SQ CQ connected to QP */
    struct doca_gpu_dev_verbs_cq cq_rq; /**< RQ CQ connected to QP */

    bool need_mcst;                /**< Receive data needs an MCST dump before it is read */
    uint8_t reserved2[7];          /**< Reserved */
    uint64_t last_get_wqe_idx;     /**< Last RDMA Read WQE posted index */
    uint64_t last_dump_wqe_idx;    /**< Last dump WQE posted index */
    uint64_t last_recv_wqe_idx;    /**< Last receive WQE posted index */

    enum doca_gpu_dev_verbs_nic_handler nic_handler; /**< NIC handler */
    enum doca_gpu_dev_verbs_mem_type mem_type;       /**< Memory type of the completion */
} __attribute__((__aligned__(8))) __attribute__((__packed__));

#ifdef __cplusplus
}
#endif

#endif /* DOCA_GPUNETIO_VERBS_DEV_H */

/** @} */
