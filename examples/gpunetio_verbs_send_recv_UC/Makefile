# SPDX-License-Identifier: BSD-3-Clause

TARGET    := gpunetio_verbs_send_recv_UC
BUILD_DIR := build

INCLUDE_DIR = -I$(TOP_DIR)/include -I$(TOP_DIR)/include/host -I$(TOP_DIR)/include/common -I../ -I$(TOP_DIR)/src -I$(CUDA_HOME)/include
CXXFLAGS = $(INCLUDE_DIR) -std=c++17 -fPIC
NVCCFLAGS = $(INCLUDE_DIR) -std=c++17 -arch=sm_$(CUDA_ARCH) -I$(TOP_DIR)/include/device
LDFLAGS = -L$(TOP_DIR)/lib -ldoca_gpunetio_host -L$(CUDA_HOME)/lib64 -libverbs -lmlx5 -lcuda -lcudart -lpthread

SRCS := $(TOP_DIR)/examples/verbs_common.cpp gpunetio_verbs_send_recv_UC_sample.cpp gpunetio_verbs_send_recv_UC_ibv.cpp gpunetio_verbs_send_recv_UC_main.cpp gpunetio_verbs_send_recv_UC_kernel.cu
OBJS := $(patsubst %.cu,$(BUILD_DIR)/%.o,$(filter %.cu,$(SRCS))) \
        $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(SRCS)))

CXX = g++
CUDA_HOME ?= /usr/local/cuda
CUDA_ARCH ?= 80

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CUDA_HOME)/bin/nvcc $(INCLUDE_DIR) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cu
	@mkdir -p $(dir $@)
	$(CUDA_HOME)/bin/nvcc $(NVCCFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
