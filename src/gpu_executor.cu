#include "db/gpu_executor.hpp"

#include <algorithm>
#include <chrono>
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

template <typename T>
void ensure_capacity(T *&d_buffer, size_t &capacity, size_t count) {
    if (count <= capacity) {
        return;
    }
    cudaFree(d_buffer);
    d_buffer = nullptr;
    capacity = 0;

    CUDA_CHECK(cudaMalloc(&d_buffer, count * sizeof(T)));
    capacity = count;
}

}

bool GpuExecutor::device_available() {
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
}

GpuExecutor::~GpuExecutor() {
    free_columns();
    free_query_buffers();
}

void GpuExecutor::free_columns() {
    for (DevicePackedColumn *column : {&d_series_ids, &d_field_ids, &d_timestamps}) {
        cudaFree(column->blocks);
        cudaFree(column->words);
        *column = DevicePackedColumn{};
    }
    cudaFree(d_field_values);

    d_field_values = nullptr;
    row_count = 0;
    uploaded_byte_count = 0;
}

void GpuExecutor::free_query_buffers() {
    cudaFree(d_series_filter);
    cudaFree(d_block_results);

    d_series_filter = nullptr;
    series_filter_capacity = 0;
    d_block_results = nullptr;
    block_results_capacity = 0;
}

void GpuExecutor::upload(const MemTable &mem_table) {
    free_columns();

    row_count = mem_table.row_count();
    if (row_count == 0) {
        return;
    }

    using Clock = std::chrono::steady_clock;

    auto start = Clock::now();
    PackedColumn series_ids = pack_column(mem_table.series_ids());
    PackedColumn field_ids = pack_column(mem_table.field_ids());
    PackedColumn timestamps = pack_column(mem_table.timestamps());
    last_compression_seconds = std::chrono::duration<double>(Clock::now() - start).count();

    start = Clock::now();
    upload_packed(d_series_ids, series_ids);
    upload_packed(d_field_ids, field_ids);
    upload_packed(d_timestamps, timestamps);

    upload_column(d_field_values, mem_table.field_values());
    uploaded_byte_count += mem_table.field_values().size() * sizeof(double);
    last_transfer_seconds = std::chrono::duration<double>(Clock::now() - start).count();
}

void GpuExecutor::upload_packed(DevicePackedColumn &d_column, const PackedColumn &column) {
    upload_column(d_column.blocks, column.blocks);
    upload_column(d_column.words, column.words);
    uploaded_byte_count += column.size_bytes();
}

namespace {

constexpr unsigned BLOCK_SIZE = 256;

constexpr int BLOCKS_PER_MULTIPROCESSOR = 8;

__global__ void scan_kernel(PackedColumnView series_ids,
                            PackedColumnView field_ids,
                            const double *__restrict__ field_values,
                            PackedColumnView timestamps,
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
        if (unpack_value(field_ids, i) != field_id) continue;
        std::uint64_t timestamp = unpack_value(timestamps, i);
        if (timestamp < from || timestamp >= to) continue;
        if (has_series_filter && series_filter[unpack_value(series_ids, i)] == 0) continue;
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

AggState GpuExecutor::run_query(const ResolvedQuery &query) {
    if (row_count == 0) {
        return AggState{};
    }

    if (multiprocessor_count == 0) {
        int device = 0;
        CUDA_CHECK(cudaGetDevice(&device));
        CUDA_CHECK(cudaDeviceGetAttribute(&multiprocessor_count, cudaDevAttrMultiProcessorCount, device));
    }

    const std::uint8_t *d_filter = nullptr;
    if (query.has_series_filter) {
        size_t filter_size = query.series_filter.size();
        ensure_capacity(d_series_filter, series_filter_capacity, filter_size);
        CUDA_CHECK(cudaMemcpy(d_series_filter, query.series_filter.data(), filter_size, cudaMemcpyHostToDevice));
        d_filter = d_series_filter;
    }

    size_t blocks_for_rows = (row_count + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t max_blocks = static_cast<size_t>(multiprocessor_count) * BLOCKS_PER_MULTIPROCESSOR;
    unsigned int blocks = static_cast<unsigned int>(std::max<size_t>(1, std::min(blocks_for_rows, max_blocks)));

    ensure_capacity(d_block_results, block_results_capacity, blocks);

    scan_kernel<<<blocks, BLOCK_SIZE>>>(d_series_ids.view(), d_field_ids.view(), d_field_values, d_timestamps.view(), row_count,
                                        query.field_id, query.from, query.to,
                                        query.has_series_filter, d_filter, d_block_results);
    CUDA_CHECK_LAUNCH();

    block_results.resize(blocks);
    CUDA_CHECK(cudaMemcpy(block_results.data(), d_block_results, blocks * sizeof(AggState), cudaMemcpyDeviceToHost));

    AggState result;
    for (const auto &block_result : block_results) {
        result.merge(block_result);
    }
    return result;
}

}
