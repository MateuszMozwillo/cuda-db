#include "db/gpu_executor.hpp"

#include <vector>

#include "db/cuda_utils.cuh"

namespace db {

namespace {

template <typename T>
void upload_column(T *&d_column, const std::vector<T> &column) {
    size_t bytes = column.size() * sizeof(T);
    CUDA_CHECK(cudaMalloc(&d_column, bytes));
    CUDA_CHECK(cudaMemcpy(d_column, column.data(), bytes, cudaMemcpyHostToDevice));
}

}

GpuExecutor::~GpuExecutor() {
    free_columns();
}

void GpuExecutor::free_columns() {
    cudaFree(d_series_ids);
    cudaFree(d_field_ids);
    cudaFree(d_field_values);
    cudaFree(d_timestamps);

    d_series_ids = nullptr;
    d_field_ids = nullptr;
    d_field_values = nullptr;
    d_timestamps = nullptr;
    row_count = 0;
}

void GpuExecutor::upload(const MemTable &mem_table) {
    free_columns();

    row_count = mem_table.row_count();
    if (row_count == 0) {
        return;
    }

    upload_column(d_series_ids, mem_table.series_ids());
    upload_column(d_field_ids, mem_table.field_ids());
    upload_column(d_field_values, mem_table.field_values());
    upload_column(d_timestamps, mem_table.timestamps());
}

namespace {

constexpr unsigned BLOCK_SIZE = 256;

__global__ void scan_kernel(const std::uint32_t *__restrict__ series_ids,
                            const std::uint32_t *__restrict__ field_ids,
                            const double *__restrict__ field_values,
                            const std::uint64_t *__restrict__ timestamps,
                            size_t row_count,
                            std::uint32_t field_id,
                            std::uint64_t from,
                            std::uint64_t to,
                            bool has_series_filter,
                            const std::uint8_t *__restrict__ series_filter,
                            AggState *__restrict__ block_results) {
    __shared__ std::uint64_t s_count[BLOCK_SIZE];
    __shared__ double s_sum[BLOCK_SIZE];
    __shared__ double s_min[BLOCK_SIZE];
    __shared__ double s_max[BLOCK_SIZE];

    const unsigned tid = threadIdx.x;

    const size_t starting_idx = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const size_t hop = static_cast<size_t>(blockDim.x) * gridDim.x;

    AggState local_agg_state;
    for (size_t i = starting_idx; i < row_count; i += hop) {
        if (field_ids[i] != field_id) continue;
        if (timestamps[i] < from || timestamps[i] >= to) continue;
        if (has_series_filter && series_filter[series_ids[i]] == 0) continue;
        local_agg_state.update(field_values[i]);
    }

    s_count[tid] = local_agg_state.count;
    s_sum[tid] = local_agg_state.sum;
    s_min[tid] = local_agg_state.min;
    s_max[tid] = local_agg_state.max;
    __syncthreads();

    for (unsigned stride = BLOCK_SIZE / 2; stride > 0; stride /= 2) {
        if (tid < stride) {
            s_count[tid] += s_count[tid + stride];
            s_sum[tid] += s_sum[tid + stride];
            s_min[tid] = fmin(s_min[tid], s_min[tid + stride]);
            s_max[tid] = fmax(s_max[tid], s_max[tid + stride]);
        }
        __syncthreads();
    }

    if (tid == 0) {
        AggState block_result;
        block_result.count = s_count[0];
        block_result.sum = s_sum[0];
        block_result.min = s_min[0];
        block_result.max = s_max[0];
        block_results[blockIdx.x] = block_result;
    }
}

}

}
