/* SPDX-License-Identifier: BSD-3-Clause */

#include <pthread.h>
#include "verbs_common.h"
#include "gpunetio_verbs_send_recv_UC.h"
#include "gpunetio_verbs_send_recv_UC_ibv.h"

#define SEND_RECV_UC_RQ_SIZE 128
#define SEND_RECV_UC_TX_PATTERN_CLIENT 0x3c
#define SEND_RECV_UC_TX_PATTERN_SERVER 0x5a
#define SEND_RECV_UC_IMMEDIATE 0x1234abcd

struct send_recv_uc_memory {
    uint8_t *tx_buf;
    uint8_t *rx_buf;
    uint8_t *dump_buf;
    struct ibv_mr *tx_mr;
    struct ibv_mr *rx_mr;
    struct ibv_mr *dump_mr;
    uint32_t *error_count;
    size_t alloc_size;
    bool rx_is_host;
};

static doca_error_t register_gpu_buffer(struct verbs_resources *resources, uint8_t *buffer,
                                        size_t size, struct ibv_mr **mr) {
    int dmabuf_fd = -1;
    *mr = nullptr;

    if (doca_gpu_get_dmabuf_fd(resources->gpu_dev, buffer, size, &dmabuf_fd) == DOCA_SUCCESS) {
        *mr = ibv_reg_dmabuf_mr(resources->verbs_pd, 0, size, (uint64_t)buffer, dmabuf_fd,
                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                                    IBV_ACCESS_RELAXED_ORDERING);
        close(dmabuf_fd);
    }
    if (*mr == nullptr) {
        *mr = ibv_reg_mr(resources->verbs_pd, buffer, size,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                             IBV_ACCESS_RELAXED_ORDERING);
    }
    return *mr == nullptr ? DOCA_ERROR_DRIVER : DOCA_SUCCESS;
}

static void destroy_local_memory(struct verbs_resources *resources,
                                 struct send_recv_uc_memory *mem) {
    if (mem->tx_mr) ibv_dereg_mr(mem->tx_mr);
    if (mem->rx_mr) ibv_dereg_mr(mem->rx_mr);
    if (mem->dump_mr) ibv_dereg_mr(mem->dump_mr);
    if (mem->tx_buf) doca_gpu_mem_free(resources->gpu_dev, mem->tx_buf);
    if (mem->rx_buf) {
        if (mem->rx_is_host)
            cudaFreeHost(mem->rx_buf);
        else
            doca_gpu_mem_free(resources->gpu_dev, mem->rx_buf);
    }
    if (mem->dump_buf) doca_gpu_mem_free(resources->gpu_dev, mem->dump_buf);
    if (mem->error_count) cudaFree(mem->error_count);
    memset(mem, 0, sizeof(*mem));
}

static doca_error_t create_local_memory(struct verbs_resources *resources,
                                        struct send_recv_uc_memory *mem, uint32_t size,
                                        uint8_t tx_pattern) {
    size_t page_size = get_page_size();
    mem->alloc_size = size > page_size ? size : page_size;
    ALIGN_SIZE(mem->alloc_size, page_size);

    doca_error_t status = doca_gpu_mem_alloc(resources->gpu_dev, mem->alloc_size, page_size,
                                             DOCA_GPU_MEM_TYPE_GPU, (void **)&mem->tx_buf, NULL);
    if (status != DOCA_SUCCESS) goto error;
    if (getenv("SEND_RECV_UC_HOST_RX") != nullptr) {
        mem->rx_is_host = true;
        if (cudaHostAlloc((void **)&mem->rx_buf, mem->alloc_size, cudaHostAllocMapped) !=
            cudaSuccess) {
            status = DOCA_ERROR_DRIVER;
            goto error;
        }
    } else {
        status = doca_gpu_mem_alloc(resources->gpu_dev, mem->alloc_size, page_size,
                                    DOCA_GPU_MEM_TYPE_GPU, (void **)&mem->rx_buf, NULL);
        if (status != DOCA_SUCCESS) goto error;
    }
    status = doca_gpu_mem_alloc(resources->gpu_dev, page_size, page_size, DOCA_GPU_MEM_TYPE_GPU,
                                (void **)&mem->dump_buf, NULL);
    if (status != DOCA_SUCCESS) goto error;

    if (cudaMemset(mem->tx_buf, tx_pattern, mem->alloc_size) != cudaSuccess ||
        cudaMemset(mem->rx_buf, 0, mem->alloc_size) != cudaSuccess ||
        cudaMemset(mem->dump_buf, 0, page_size) != cudaSuccess) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }

    status = register_gpu_buffer(resources, mem->tx_buf, mem->alloc_size, &mem->tx_mr);
    if (status != DOCA_SUCCESS) goto error;
    status = register_gpu_buffer(resources, mem->rx_buf, mem->alloc_size, &mem->rx_mr);
    if (status != DOCA_SUCCESS) goto error;
    status = register_gpu_buffer(resources, mem->dump_buf, page_size, &mem->dump_mr);
    if (status != DOCA_SUCCESS) goto error;

    if (cudaMalloc((void **)&mem->error_count, sizeof(uint32_t)) != cudaSuccess ||
        cudaMemset(mem->error_count, 0, sizeof(uint32_t)) != cudaSuccess) {
        status = DOCA_ERROR_DRIVER;
        goto error;
    }
    return DOCA_SUCCESS;

error:
    destroy_local_memory(resources, mem);
    return status;
}

static doca_error_t exchange_qp_info(struct verbs_resources *resources) {
    if (send(resources->conn_socket, &resources->local_qp_number, sizeof(uint32_t), 0) < 0 ||
        recv(resources->conn_socket, &resources->remote_qp_number, sizeof(uint32_t), 0) < 0 ||
        send(resources->conn_socket, &resources->gid.raw, sizeof(resources->gid.raw), 0) < 0 ||
        recv(resources->conn_socket, &resources->remote_gid.raw, sizeof(resources->gid.raw), 0) <
            0 ||
        send(resources->conn_socket, &resources->lid, sizeof(resources->lid), 0) < 0 ||
        recv(resources->conn_socket, &resources->dlid, sizeof(resources->dlid), 0) < 0) {
        DOCA_LOG(LOG_ERR, "Failed to exchange QP/GID information: %d", errno);
        return DOCA_ERROR_CONNECTION_ABORTED;
    }
    return DOCA_SUCCESS;
}

static doca_error_t wait_for_rq_submission(struct doca_gpu_verbs_qp *qp, uint32_t num_recvs,
                                           bool proxy_enabled) {
    if (!proxy_enabled) return DOCA_SUCCESS;

    for (uint32_t retry = 0; retry < 5000; ++retry) {
        uint64_t submitted = __atomic_load_n(&qp->rq_wqe_pi_last, __ATOMIC_RELAXED);
        if (submitted >= num_recvs) return DOCA_SUCCESS;
        usleep(1000);
    }
    DOCA_LOG(LOG_ERR, "Timed out waiting for RQ doorbell progression");
    return DOCA_ERROR_TIME_OUT;
}

static doca_error_t exchange_ready(struct verbs_resources *resources) {
    uint32_t ready = 1;
    uint32_t remote_ready = 0;
    if (send(resources->conn_socket, &ready, sizeof(ready), 0) != sizeof(ready) ||
        recv(resources->conn_socket, &remote_ready, sizeof(remote_ready), 0) !=
            sizeof(remote_ready) ||
        remote_ready != 1) {
        DOCA_LOG(LOG_ERR, "Failed to exchange receive-ready barrier");
        return DOCA_ERROR_CONNECTION_ABORTED;
    }
    return DOCA_SUCCESS;
}

static doca_error_t run_send_recv_uc(struct verbs_config *cfg) {
    struct verbs_resources resources = {0};
    struct send_recv_uc_memory mem = {0};
    struct cpu_proxy_args proxy_args = {0};
    struct doca_gpu_dev_verbs_qp *qp_gpu = nullptr;
    struct doca_gpu_verbs_qp *active_gverbs = nullptr;
    cudaStream_t stream = nullptr;
    pthread_t proxy_thread;
    bool proxy_started = false;
    bool proxy_enabled = false;
    int server_sock_fd = -1;
    uint32_t errors = 0;
    doca_error_t status = DOCA_SUCCESS;
    doca_error_t tmp_status;

    const bool is_client = !cfg->is_server;
    const uint8_t tx_pattern =
        is_client ? SEND_RECV_UC_TX_PATTERN_CLIENT : SEND_RECV_UC_TX_PATTERN_SERVER;
    const uint8_t expected_byte =
        is_client ? SEND_RECV_UC_TX_PATTERN_SERVER : SEND_RECV_UC_TX_PATTERN_CLIENT;

    resources.conn_socket = -1;
    resources.num_iters = cfg->num_iters;
    resources.cuda_threads = 1;
    resources.nic_handler = cfg->nic_handler;
    resources.send_dbr_mode_ext = cfg->send_dbr_mode_ext;
    resources.cq_collapsed = 0;
    resources.enable_umem_cpu = false;
    resources.qp_group = false;
    cfg->rq_nwqe = SEND_RECV_UC_RQ_SIZE;
    cfg->qp_type = DOCA_VERBS_QP_TYPE_UC;

    if (cfg->use_ibv_backend)
        status = send_recv_uc_ibv_create(cfg, &resources);
    else
        status = create_verbs_resources(cfg, &resources);
    if (status != DOCA_SUCCESS) return status;

    status = create_local_memory(&resources, &mem, cfg->message_size, tx_pattern);
    if (status != DOCA_SUCCESS) goto cleanup;

    if (cfg->is_server)
        status = oob_verbs_connection_server_setup(&server_sock_fd, &resources.conn_socket) == 0
                     ? DOCA_SUCCESS
                     : DOCA_ERROR_CONNECTION_ABORTED;
    else
        status = oob_verbs_connection_client_setup(cfg->server_ip_addr.c_str(),
                                                   &resources.conn_socket) == 0
                     ? DOCA_SUCCESS
                     : DOCA_ERROR_CONNECTION_ABORTED;
    if (status != DOCA_SUCCESS) goto cleanup;

    status = exchange_qp_info(&resources);
    if (status != DOCA_SUCCESS) goto cleanup;
    if (cfg->use_ibv_backend) {
        status = send_recv_uc_ibv_connect(&resources);
        if (status == DOCA_SUCCESS) status = send_recv_uc_ibv_export(&resources, &qp_gpu);
    } else {
        status = connect_verbs_qp(&resources);
        if (status == DOCA_SUCCESS)
            status = doca_gpu_verbs_get_qp_dev(resources.qp->qp_gverbs, &qp_gpu);
    }
    if (status != DOCA_SUCCESS) goto cleanup;

    active_gverbs = cfg->use_ibv_backend ? resources.ibv_qp_gverbs : resources.qp->qp_gverbs;

    if (cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) != cudaSuccess) {
        status = DOCA_ERROR_DRIVER;
        goto cleanup;
    }

    doca_gpu_verbs_cpu_proxy_enabled(active_gverbs, &proxy_enabled);
    if (proxy_enabled) {
        proxy_args.qp_cpu = active_gverbs;
        proxy_args.qp_cpu_companion = nullptr;
        proxy_args.exit_flag = (uint64_t *)calloc(1, sizeof(uint64_t));
        if (proxy_args.exit_flag == nullptr ||
            pthread_create(&proxy_thread, NULL, progress_cpu_proxy, &proxy_args) != 0) {
            status = DOCA_ERROR_DRIVER;
            goto cleanup;
        }
        proxy_started = true;
    }

    /* UC has no retransmission: every receive must be posted before either peer sends. */
    status = gpunetio_verbs_send_recv_uc_prepare(stream, qp_gpu, cfg->num_iters,
                                                 cfg->message_size, cfg->use_low_level, mem.rx_buf,
                                                 htobe32(mem.rx_mr->lkey));
    if (status == DOCA_SUCCESS && cudaStreamSynchronize(stream) != cudaSuccess)
        status = DOCA_ERROR_DRIVER;
    if (status == DOCA_SUCCESS)
        status = wait_for_rq_submission(active_gverbs, cfg->num_iters, proxy_enabled);
    if (status == DOCA_SUCCESS) status = exchange_ready(&resources);
    if (status != DOCA_SUCCESS) goto cleanup;

    printf("Starting UC Send/Receive test: api=%s, opcode=%s, size=%u, iterations=%u, NIC=%s\n",
           cfg->use_low_level ? "low" : "high", cfg->use_immediate ? "SEND_IMM" : "SEND",
           cfg->message_size, cfg->num_iters,
           doca_gpu_nic_handler_to_string(active_gverbs->qp_cpu->nic_handler));
    fflush(stdout);

    status = gpunetio_verbs_send_recv_uc(
        stream, qp_gpu, cfg->num_iters, cfg->message_size, cfg->use_low_level,
        cfg->use_immediate, is_client, mem.tx_buf, htobe32(mem.tx_mr->lkey), mem.rx_buf,
        htobe32(mem.rx_mr->lkey), mem.dump_buf, htobe32(mem.dump_mr->lkey), expected_byte,
        SEND_RECV_UC_IMMEDIATE, mem.error_count);
    if (status == DOCA_SUCCESS && cudaStreamSynchronize(stream) != cudaSuccess)
        status = DOCA_ERROR_DRIVER;
    if (status == DOCA_SUCCESS &&
        cudaMemcpy(&errors, mem.error_count, sizeof(errors), cudaMemcpyDeviceToHost) != cudaSuccess)
        status = DOCA_ERROR_DRIVER;

    if (proxy_started) {
        *((volatile uint64_t *)proxy_args.exit_flag) = 1;
        pthread_join(proxy_thread, NULL);
        proxy_started = false;
    }

    if (status == DOCA_SUCCESS && errors == 0) {
        printf("UC Send/Receive validation successful: %s, %s, %u iterations x %u bytes\n",
               cfg->use_low_level ? "low-level API" : "high-level API",
               cfg->use_immediate ? "SEND_IMM" : "SEND", cfg->num_iters, cfg->message_size);
    } else {
        DOCA_LOG(LOG_ERR, "UC Send/Receive validation failed: status=%d errors=%u", status,
                 errors);
        if (status == DOCA_SUCCESS) status = DOCA_ERROR_BAD_STATE;
    }

cleanup:
    if (proxy_started) {
        *((volatile uint64_t *)proxy_args.exit_flag) = 1;
        pthread_join(proxy_thread, NULL);
    }
    if (proxy_args.exit_flag) free(proxy_args.exit_flag);
    if (stream) cudaStreamDestroy(stream);
    if (cfg->is_server)
        oob_verbs_connection_server_close(server_sock_fd, resources.conn_socket);
    else
        oob_verbs_connection_client_close(resources.conn_socket);
    destroy_local_memory(&resources, &mem);
    if (cfg->use_ibv_backend) {
        send_recv_uc_ibv_destroy(&resources);
        tmp_status = DOCA_SUCCESS;
    } else {
        tmp_status = destroy_verbs_resources(&resources);
    }
    if (status == DOCA_SUCCESS && tmp_status != DOCA_SUCCESS) status = tmp_status;
    return status;
}

doca_error_t verbs_server(struct verbs_config *cfg) { return run_send_recv_uc(cfg); }

doca_error_t verbs_client(struct verbs_config *cfg) { return run_send_recv_uc(cfg); }
