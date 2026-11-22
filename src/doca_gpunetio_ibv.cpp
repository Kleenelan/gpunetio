/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cuda_runtime.h>
#include <infiniband/mlx5dv.h>
#include <string.h>

#include "doca_internal.hpp"
#include "host/doca_gpunetio.h"
#include "common/doca_gpunetio_verbs_dev.h"
#include "doca_gpunetio.hpp"

static size_t priv_ibv_page_size() {
    long ret = sysconf(_SC_PAGESIZE);
    return ret == -1 ? 4096 : (size_t)ret;
}

void doca_gpu_verbs_ibv_cleanup_mappings(struct doca_gpu_verbs_qp *qp) {
    if (qp == nullptr) return;
    for (uint32_t i = 0; i < qp->ibv_map_num; ++i) {
        if (qp->ibv_map_base[i] != nullptr) cudaHostUnregister(qp->ibv_map_base[i]);
    }
    qp->ibv_map_num = 0;
}

static doca_error_t priv_ibv_map(struct doca_gpu_verbs_qp *qp, void *addr, size_t size, bool io,
                                 void **host_ptr, void **gpu_ptr) {
    const size_t page_size = priv_ibv_page_size();
    const uintptr_t page_mask = ~(uintptr_t)(page_size - 1);
    const uintptr_t base = (uintptr_t)addr & page_mask;
    const size_t map_size = ((uintptr_t)addr + size - base + page_size - 1) & page_mask;
    void *base_gpu = nullptr;

    for (uint32_t i = 0; i < qp->ibv_map_num; ++i) {
        if ((uintptr_t)qp->ibv_map_base[i] == base) {
            cudaError_t err = cudaHostGetDevicePointer(&base_gpu, (void *)base, 0);
            if (err != cudaSuccess) return DOCA_ERROR_DRIVER;
            *host_ptr = addr;
            *gpu_ptr = (void *)((uintptr_t)base_gpu + ((uintptr_t)addr - base));
            return DOCA_SUCCESS;
        }
    }

    unsigned int flags = cudaHostRegisterPortable | cudaHostRegisterMapped;
    if (io) flags |= cudaHostRegisterIoMemory;
    cudaError_t err = cudaHostRegister((void *)base, map_size, flags);
    if (err != cudaSuccess && err != cudaErrorHostMemoryAlreadyRegistered &&
        err != cudaErrorMemoryAllocation) {
        DOCA_LOG(LOG_ERR, "cudaHostRegister(%p, %zu) failed: %s", (void *)base, map_size,
                 cudaGetErrorString(err));
        return DOCA_ERROR_DRIVER;
    }
    err = cudaHostGetDevicePointer(&base_gpu, (void *)base, 0);
    if (err != cudaSuccess) {
        DOCA_LOG(LOG_ERR, "cudaHostGetDevicePointer(%p) failed: %s", (void *)base,
                 cudaGetErrorString(err));
        return DOCA_ERROR_DRIVER;
    }

    // Own only mappings created by this export. Overlapping mappings are reused.
    if (qp->ibv_map_num < sizeof(qp->ibv_map_base) / sizeof(qp->ibv_map_base[0])) {
        bool already_mapped = false;
        for (uint32_t i = 0; i < qp->ibv_map_num; ++i)
            already_mapped |= ((uintptr_t)qp->ibv_map_base[i] == base);
        if (!already_mapped) qp->ibv_map_base[qp->ibv_map_num++] = (void *)base;
    }

    *host_ptr = addr;
    *gpu_ptr = (void *)((uintptr_t)base_gpu + ((uintptr_t)addr - base));
    return DOCA_SUCCESS;
}

static void priv_ibv_init_cq(struct doca_gpu_dev_verbs_cq *out, struct mlx5dv_cq *src,
                             void *cqe_gpu, void *dbrec_gpu) {
    auto *cqe = (struct doca_gpunetio_ib_mlx5_cqe64 *)src->buf;
    for (uint32_t i = 0; i < src->cqe_cnt; ++i) {
        cqe[i].op_own = (DOCA_GPUNETIO_IB_MLX5_CQE_INVALID
                         << DOCA_GPUNETIO_VERBS_MLX5_CQE_OPCODE_SHIFT) |
                        DOCA_GPUNETIO_IB_MLX5_CQE_OWNER_MASK;
    }

    out->cqe_daddr = (uint8_t *)cqe_gpu;
    out->cq_num = src->cqn;
    out->cqe_num = src->cqe_cnt;
    out->cqe_size = src->cqe_size;
    out->cqe_mask = out->cqe_num - 1;
    out->dbrec = (__be32 *)dbrec_gpu;
    out->cqe_ci = 0;
    out->cqe_rsvd = 0;
    out->mem_type = DOCA_GPUNETIO_VERBS_MEM_TYPE_HOST;
}

doca_error_t doca_gpu_verbs_ibv_export_qp(doca_gpu_t *gpu_dev, struct ibv_qp *qp,
                                          struct ibv_cq *cq_sq, struct ibv_cq *cq_rq,
                                          enum doca_gpu_dev_verbs_nic_handler nic_handler,
                                          struct doca_gpu_verbs_qp **qp_out) {
    if (gpu_dev == nullptr || gpu_dev->open == nullptr || qp == nullptr || cq_sq == nullptr ||
        cq_rq == nullptr || qp_out == nullptr)
        return DOCA_ERROR_INVALID_VALUE;

    doca_error_t status = DOCA_SUCCESS;
    struct doca_gpu_verbs_qp *out =
        (struct doca_gpu_verbs_qp *)calloc(1, sizeof(struct doca_gpu_verbs_qp));
    if (out == nullptr) return DOCA_ERROR_NO_MEMORY;
    out->qp_cpu =
        (struct doca_gpu_dev_verbs_qp *)calloc(1, sizeof(struct doca_gpu_dev_verbs_qp));
    if (out->qp_cpu == nullptr) {
        free(out);
        return DOCA_ERROR_NO_MEMORY;
    }

    out->gpu_dev = gpu_dev;
    out->ibv_qp = qp;
    out->ibv_cq_sq = cq_sq;
    out->ibv_cq_rq = cq_rq;
    out->send_dbr_mode_ext = DOCA_GPUNETIO_VERBS_SEND_DBR_MODE_EXT_VALID_DBR;

    auto *q = out->qp_cpu;
    bool force_proxy = (nic_handler == DOCA_GPUNETIO_VERBS_NIC_HANDLER_CPU_PROXY);
    mlx5dv_qp dvqp = {};
    mlx5dv_obj qp_obj = {};
    qp_obj.qp.in = qp;
    qp_obj.qp.out = &dvqp;
    mlx5dv_cq dvscq = {}, dvrcq = {};
    mlx5dv_obj cq_obj = {};
    void *rq_gpu = nullptr, *sq_gpu = nullptr, *dbrec_gpu = nullptr, *bf_gpu = nullptr;
    void *scqe_gpu = nullptr, *rcqe_gpu = nullptr, *scq_dbrec_gpu = nullptr,
           *rcq_dbrec_gpu = nullptr;
    void *host_ptr = nullptr;

    if (mlx5dv_init_obj(&qp_obj, MLX5DV_OBJ_QP) != 0) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }

    cq_obj.cq.in = cq_sq;
    cq_obj.cq.out = &dvscq;
    if (mlx5dv_init_obj(&cq_obj, MLX5DV_OBJ_CQ) != 0) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }
    cq_obj.cq.in = cq_rq;
    cq_obj.cq.out = &dvrcq;
    if (mlx5dv_init_obj(&cq_obj, MLX5DV_OBJ_CQ) != 0) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }

    status = priv_ibv_map(out, dvqp.rq.buf, (size_t)dvqp.rq.wqe_cnt * dvqp.rq.stride, false,
                          &host_ptr, &rq_gpu);
    if (status != DOCA_SUCCESS) goto error;
    status = priv_ibv_map(out, dvqp.sq.buf, (size_t)dvqp.sq.wqe_cnt * dvqp.sq.stride, false,
                          &host_ptr, &sq_gpu);
    if (status != DOCA_SUCCESS) goto error;
    status = priv_ibv_map(out, dvqp.dbrec, 8, false, &host_ptr, &dbrec_gpu);
    if (status != DOCA_SUCCESS) goto error;
    status = priv_ibv_map(out, dvscq.buf, (size_t)dvscq.cqe_cnt * dvscq.cqe_size, false,
                          &host_ptr, &scqe_gpu);
    if (status != DOCA_SUCCESS) goto error;
    status = priv_ibv_map(out, dvscq.dbrec, 4, false, &host_ptr, &scq_dbrec_gpu);
    if (status != DOCA_SUCCESS) goto error;
    status = priv_ibv_map(out, dvrcq.buf, (size_t)dvrcq.cqe_cnt * dvrcq.cqe_size, false,
                          &host_ptr, &rcqe_gpu);
    if (status != DOCA_SUCCESS) goto error;
    status = priv_ibv_map(out, dvrcq.dbrec, 4, false, &host_ptr, &rcq_dbrec_gpu);
    if (status != DOCA_SUCCESS) goto error;

    q->sq_num = qp->qp_num;
    q->sq_num_shift8 = q->sq_num << 8;
    q->sq_num_shift8_be = htobe32(q->sq_num_shift8);
    q->sq_num_shift8_be_1ds = htobe32(q->sq_num_shift8 | 1);
    q->sq_num_shift8_be_2ds = htobe32(q->sq_num_shift8 | 2);
    q->sq_num_shift8_be_3ds = htobe32(q->sq_num_shift8 | 3);
    q->sq_num_shift8_be_4ds = htobe32(q->sq_num_shift8 | 4);
    q->sq_num_shift8_be_5ds = htobe32(q->sq_num_shift8 | 5);
    q->sq_wqe_num = dvqp.sq.wqe_cnt;
    q->sq_wqe_mask = q->sq_wqe_num - 1;
    q->sq_wqe_daddr = (uint8_t *)sq_gpu;
    q->sq_dbrec = (__be32 *)dbrec_gpu + DOCA_GPUNETIO_IB_MLX5_SND_DBR;
    q->sq_db = (uint64_t *)bf_gpu;
    q->sq_wqe_pi = 0;
    q->sq_rsvd_index = 0;
    q->sq_ready_index = 0;
    q->sq_lock = 0;

    q->rq_num = q->sq_num;
    q->rq_wqe_num = dvqp.rq.wqe_cnt;
    q->rq_wqe_mask = q->rq_wqe_num - 1;
    q->rq_wqe_daddr = (uint8_t *)rq_gpu;
    q->rq_dbrec = (__be32 *)dbrec_gpu + DOCA_GPUNETIO_IB_MLX5_RCV_DBR;
    q->rq_db = q->sq_db;
    q->rcv_wqe_size = dvqp.rq.stride;
    q->rq_wqe_pi = 0;
    q->rq_rsvd_index = 0;
    q->rq_ready_index = 0;
    q->rq_lock = 0;
    q->need_mcst = gpu_dev->open->need_mcst;
    q->mem_type = DOCA_GPUNETIO_VERBS_MEM_TYPE_HOST;

    priv_ibv_init_cq(&q->cq_sq, &dvscq, scqe_gpu, scq_dbrec_gpu);
    priv_ibv_init_cq(&q->cq_rq, &dvrcq, rcqe_gpu, rcq_dbrec_gpu);

    out->sq_dbrec = q->sq_dbrec;
    out->sq_db = (uint64_t *)dvqp.bf.reg;
    out->sq_num_shift8_be = q->sq_num_shift8_be;
    out->sq_wqe_pi_last = 0;
    out->rq_wqe_pi_last = 0;

    if (!force_proxy) {
        uint64_t *uar_gpu = nullptr;
        status = doca_gpu_verbs_export_uar((uint64_t *)dvqp.bf.reg, &uar_gpu);
        if (status != DOCA_SUCCESS && nic_handler != DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO) {
            goto error;
        }
        if (status == DOCA_SUCCESS) {
            q->sq_db = uar_gpu;
            q->rq_db = q->sq_db;
            q->nic_handler = (nic_handler == DOCA_GPUNETIO_VERBS_NIC_HANDLER_GPU_SM_NO_DBR)
                                 ? DOCA_GPUNETIO_VERBS_NIC_HANDLER_GPU_SM_NO_DBR
                                 : DOCA_GPUNETIO_VERBS_NIC_HANDLER_GPU_SM_DB;
        } else {
            force_proxy = true;
        }
    }

    if (force_proxy) {
        q->sq_dbrec = (__be32 *)dvqp.dbrec + DOCA_GPUNETIO_IB_MLX5_SND_DBR;
        q->rq_dbrec = (__be32 *)dvqp.dbrec + DOCA_GPUNETIO_IB_MLX5_RCV_DBR;
        out->sq_dbrec = q->sq_dbrec;

        status = doca_gpu_mem_alloc(gpu_dev, sizeof(uint64_t), priv_ibv_page_size(),
                                    DOCA_GPU_MEM_TYPE_CPU_GPU, (void **)&out->cpu_db_gpu,
                                    (void **)&out->cpu_db);
        if (status != DOCA_SUCCESS) goto error;
        *out->cpu_db = 0;
        status = doca_gpu_mem_alloc(gpu_dev, sizeof(uint64_t), priv_ibv_page_size(),
                                    DOCA_GPU_MEM_TYPE_CPU_GPU, (void **)&out->cpu_db_rq_gpu,
                                    (void **)&out->cpu_db_rq);
        if (status != DOCA_SUCCESS) goto error;
        *out->cpu_db_rq = 0;
        q->sq_db = out->cpu_db_gpu;
        q->rq_db = out->cpu_db_rq_gpu;
        q->nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_CPU_PROXY;
        out->cpu_proxy = true;
    } else {
        out->cpu_proxy = false;
    }

    *qp_out = out;
    return DOCA_SUCCESS;

error:
    if (out->cpu_db_gpu) doca_gpu_mem_free(gpu_dev, out->cpu_db_gpu);
    if (out->cpu_db_rq_gpu) doca_gpu_mem_free(gpu_dev, out->cpu_db_rq_gpu);
    if (out->qp_cpu) {
        if (out->cpu_proxy == false && out->sq_db != nullptr)
            doca_gpu_verbs_unexport_uar(out->sq_db);
        free(out->qp_cpu);
    }
    doca_gpu_verbs_ibv_cleanup_mappings(out);
    free(out);
    return status;
}
