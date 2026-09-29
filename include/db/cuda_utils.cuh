#pragma once

#include <cstdio>
#include <cstdlib>

#include <cuda_runtime.h>

#define CUDA_CHECK(call)                                                                \
    do {                                                                                \
        cudaError_t err = (call);                                                       \
        if (err != cudaSuccess) {                                                       \
            std::fprintf(stderr, "CUDA error %s at %s:%d: %s\n", cudaGetErrorName(err), \
                         __FILE__, __LINE__, cudaGetErrorString(err));                  \
            std::abort();                                                               \
        }                                                                               \
    } while (0)

#define CUDA_CHECK_LAUNCH() CUDA_CHECK(cudaGetLastError())
