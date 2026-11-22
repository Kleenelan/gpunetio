/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdlib.h>
#include <string.h>
#include "verbs_common.h"

static void usage(const char *program) {
    std::cerr << "Usage: " << program << "\n"
              << " -c <server_ip>                 Client mode (default: server)\n"
              << " -d <mlx5_X>                    RDMA device\n"
              << " -g <GPU PCIe address>          GPU PCIe address\n"
              << " -i <iterations>                Ping-pong iterations (default: 4)\n"
              << " -l <GID index>                 GID index (default: 3)\n"
              << " -p <NIC handler>               0:AUTO 1:CPU_PROXY 2:GPU_SM_DB (default: 0)\n"
              << " -s <bytes>                     Message size, including 0 (default: 64)\n"
              << " -a <high|low>                  API level (default: high)\n"
              << " -b <ibv|devx>                  QP backend (default: ibv)\n"
              << " -m                             Use SEND_IMM (low-level API only)\n"
              << " -r <DBR mode>                  Send DBR mode (default: 0)\n";
}

int main(int argc, char **argv) {
    struct verbs_config cfg;
    cfg.is_server = true;
    cfg.gid_index = 3;
    cfg.num_iters = 4;
    cfg.cuda_threads = 1;
    cfg.nic_handler = DOCA_GPUNETIO_VERBS_NIC_HANDLER_AUTO;
    cfg.send_dbr_mode_ext = DOCA_GPUNETIO_VERBS_SEND_DBR_MODE_EXT_VALID_DBR;
    cfg.message_size = 64;
    cfg.use_low_level = false;
    cfg.use_immediate = false;
    cfg.use_ibv_backend = true;

    int option;
    while ((option = getopt(argc, argv, "c:d:g:i:l:p:s:a:b:mr:")) != -1) {
        switch (option) {
            case 'c':
                cfg.server_ip_addr = optarg;
                cfg.is_server = false;
                break;
            case 'd':
                cfg.nic_device_name = optarg;
                break;
            case 'g':
                cfg.gpu_pcie_addr = optarg;
                break;
            case 'i':
                cfg.num_iters = std::atoi(optarg);
                break;
            case 'l':
                cfg.gid_index = std::atoi(optarg);
                break;
            case 'p':
                cfg.nic_handler = (enum doca_gpu_dev_verbs_nic_handler)std::atoi(optarg);
                break;
            case 's':
                cfg.message_size = std::atoi(optarg);
                break;
            case 'a':
                if (strcmp(optarg, "low") == 0)
                    cfg.use_low_level = true;
                else if (strcmp(optarg, "high") == 0)
                    cfg.use_low_level = false;
                else {
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'b':
                if (strcmp(optarg, "ibv") == 0)
                    cfg.use_ibv_backend = true;
                else if (strcmp(optarg, "devx") == 0)
                    cfg.use_ibv_backend = false;
                else {
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                break;
            case 'm':
                cfg.use_immediate = true;
                break;
            case 'r':
                cfg.send_dbr_mode_ext =
                    (enum doca_gpu_verbs_send_dbr_mode_ext)std::atoi(optarg);
                break;
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (cfg.nic_device_name.empty() || cfg.gpu_pcie_addr.empty() || cfg.num_iters == 0 ||
        cfg.num_iters > 128) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (cfg.use_immediate && !cfg.use_low_level) {
        std::cerr << "-m requires '-a low'\n";
        return EXIT_FAILURE;
    }

    doca_error_t status = cfg.is_server ? verbs_server(&cfg) : verbs_client(&cfg);
    return status == DOCA_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
