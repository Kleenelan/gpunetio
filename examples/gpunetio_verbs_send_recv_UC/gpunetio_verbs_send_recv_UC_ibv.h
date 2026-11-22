/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef GPUNETIO_VERBS_SEND_RECV_UC_IBV_H_
#define GPUNETIO_VERBS_SEND_RECV_UC_IBV_H_

#include "verbs_common.h"

doca_error_t send_recv_uc_ibv_create(struct verbs_config *cfg,
                                     struct verbs_resources *resources);
doca_error_t send_recv_uc_ibv_connect(struct verbs_resources *resources);
doca_error_t send_recv_uc_ibv_export(struct verbs_resources *resources,
                                     struct doca_gpu_dev_verbs_qp **qp_gpu);
void send_recv_uc_ibv_destroy(struct verbs_resources *resources);

#endif /* GPUNETIO_VERBS_SEND_RECV_UC_IBV_H_ */
