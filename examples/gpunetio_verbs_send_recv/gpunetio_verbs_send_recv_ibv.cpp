/* SPDX-License-Identifier: BSD-3-Clause */

#include <cuda_runtime.h>
#include <string.h>
#include "gpunetio_verbs_send_recv_ibv.h"

static struct ibv_context *send_recv_open_ib_device(const char *name) {
    int num_devices = 0;
    struct ibv_device **device_list = ibv_get_device_list(&num_devices);
    if (device_list == nullptr) return nullptr;

    struct ibv_context *ctx = nullptr;
    for (int i = 0; i < num_devices; ++i) {
        if (strcmp(ibv_get_device_name(device_list[i]), name) == 0) {
            ctx = ibv_open_device(device_list[i]);
            break;
        }
    }
    ibv_free_device_list(device_list);
    return ctx;
}

doca_error_t send_recv_ibv_create(struct verbs_config *cfg, struct verbs_resources *resources) {
    doca_error_t status = DOCA_SUCCESS;
    int cuda_id = 0;
    union ibv_gid rgid;
    struct ibv_port_attr port_attr;
    struct ibv_qp_init_attr qp_attr = {};

    resources->cfg = cfg;
    cudaFree(0);
    if (cudaDeviceGetByPCIBusId(&cuda_id, cfg->gpu_pcie_addr.c_str()) != cudaSuccess)
        return DOCA_ERROR_INVALID_VALUE;
    cudaSetDevice(cuda_id);

    status = doca_gpu_create(cfg->gpu_pcie_addr.c_str(), &resources->gpu_dev);
    if (status != DOCA_SUCCESS) return status;

    resources->verbs_context = send_recv_open_ib_device(cfg->nic_device_name.c_str());
    if (resources->verbs_context == nullptr) {
        status = DOCA_ERROR_BAD_STATE;
        goto error;
    }
    resources->verbs_pd = ibv_alloc_pd(resources->verbs_context);
    if (resources->verbs_pd == nullptr) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }
    if (ibv_query_gid(resources->verbs_pd->context, 1, cfg->gid_index, &rgid) != 0) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }
    memcpy(resources->gid.raw, rgid.raw, DOCA_VERBS_GID_BYTE_LENGTH);
    if (ibv_query_port(resources->verbs_pd->context, 1, &port_attr) != 0) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }
    resources->lid = port_attr.lid;

    resources->ibv_scq = ibv_create_cq(resources->verbs_context, 128, nullptr, nullptr, 0);
    resources->ibv_rcq = ibv_create_cq(resources->verbs_context, 128, nullptr, nullptr, 0);
    if (resources->ibv_scq == nullptr || resources->ibv_rcq == nullptr) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }

    qp_attr.send_cq = resources->ibv_scq;
    qp_attr.recv_cq = resources->ibv_rcq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 128;
    qp_attr.cap.max_recv_wr = 128;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    resources->ibv_qp = ibv_create_qp(resources->verbs_pd, &qp_attr);
    if (resources->ibv_qp == nullptr) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }
    resources->local_qp_number = resources->ibv_qp->qp_num;
    return DOCA_SUCCESS;

error:
    send_recv_ibv_destroy(resources);
    return status;
}

doca_error_t send_recv_ibv_connect(struct verbs_resources *resources) {
    struct ibv_qp_attr attr = {};
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = 1;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(resources->ibv_qp, &attr,
                      IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS) != 0)
        return DOCA_ERROR_DRIVER;

    attr = {};
    attr.qp_state = IBV_QPS_RTR;
    attr.dest_qp_num = resources->remote_qp_number;
    attr.rq_psn = 0;
    attr.path_mtu = IBV_MTU_4096;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 1;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.dlid = resources->dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;
    attr.ah_attr.grh.dgid = *(union ibv_gid *)resources->remote_gid.raw;
    attr.ah_attr.grh.sgid_index = 3;
    attr.ah_attr.grh.hop_limit = 255;
    if (ibv_modify_qp(resources->ibv_qp, &attr,
                      IBV_QP_STATE | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_PATH_MTU |
                          IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | IBV_QP_AV) != 0)
        return DOCA_ERROR_DRIVER;

    attr = {};
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    attr.alt_timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = 1;
    if (ibv_modify_qp(resources->ibv_qp, &attr,
                      IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                          IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC) != 0)
        return DOCA_ERROR_DRIVER;

    return DOCA_SUCCESS;
}

doca_error_t send_recv_ibv_export(struct verbs_resources *resources,
                                  struct doca_gpu_dev_verbs_qp **qp_gpu) {
    doca_error_t status = doca_gpu_verbs_ibv_export_qp(
        resources->gpu_dev, resources->ibv_qp, resources->ibv_scq, resources->ibv_rcq,
        resources->nic_handler, &resources->ibv_qp_gverbs);
    if (status != DOCA_SUCCESS) return status;
    return doca_gpu_verbs_get_qp_dev(resources->ibv_qp_gverbs, qp_gpu);
}

void send_recv_ibv_destroy(struct verbs_resources *resources) {
    if (resources->ibv_qp_gverbs) {
        doca_gpu_verbs_unexport_qp(resources->gpu_dev, resources->ibv_qp_gverbs);
        resources->ibv_qp_gverbs = nullptr;
    }
    if (resources->ibv_qp) {
        ibv_destroy_qp(resources->ibv_qp);
        resources->ibv_qp = nullptr;
    }
    if (resources->ibv_scq) {
        ibv_destroy_cq(resources->ibv_scq);
        resources->ibv_scq = nullptr;
    }
    if (resources->ibv_rcq) {
        ibv_destroy_cq(resources->ibv_rcq);
        resources->ibv_rcq = nullptr;
    }
    if (resources->verbs_pd) {
        ibv_dealloc_pd(resources->verbs_pd);
        resources->verbs_pd = nullptr;
    }
    if (resources->verbs_context) {
        ibv_close_device(resources->verbs_context);
        resources->verbs_context = nullptr;
    }
    if (resources->gpu_dev) {
        doca_gpu_destroy(resources->gpu_dev);
        resources->gpu_dev = nullptr;
    }
}
