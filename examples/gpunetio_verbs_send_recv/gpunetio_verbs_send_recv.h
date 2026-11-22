/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef GPUNETIO_VERBS_SEND_RECV_H_
#define GPUNETIO_VERBS_SEND_RECV_H_

#include <stdbool.h>
#include <stdint.h>
#include <cuda_runtime_api.h>
#include "doca_gpunetio_verbs_dev.h"
#include "doca_error.h"

#ifdef __cplusplus
extern "C" {
#endif

doca_error_t gpunetio_verbs_send_recv_prepare(cudaStream_t stream,
                                              struct doca_gpu_dev_verbs_qp *qp,
                                              uint32_t num_recvs, uint32_t size,
                                              bool use_low_level, uint8_t *rx_buf,
                                              uint32_t rx_mkey);

doca_error_t gpunetio_verbs_send_recv(cudaStream_t stream, struct doca_gpu_dev_verbs_qp *qp,
                                      uint32_t num_iters, uint32_t size, bool use_low_level,
                                      bool use_immediate, bool is_client, uint8_t *tx_buf,
                                      uint32_t tx_mkey, uint8_t *rx_buf, uint32_t rx_mkey,
                                      uint8_t *dump_buf, uint32_t dump_mkey, uint8_t expected_byte,
                                      uint32_t expected_imm, uint32_t *error_count);

#ifdef __cplusplus
}
#endif

#endif /* GPUNETIO_VERBS_SEND_RECV_H_ */
