#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/mem_table.hpp"
#include "db/query.hpp"

namespace db {

class GpuExecutor {
public:
    GpuExecutor() = default;
    ~GpuExecutor();

    GpuExecutor(const GpuExecutor &) = delete;
    GpuExecutor &operator=(const GpuExecutor &) = delete;

    void upload(const MemTable &mem_table);

    AggState run_query(const ResolvedQuery &query);

    size_t uploaded_rows() const { return row_count; }

    static bool device_available();

    // creates the CUDA context, otherwise the first CUDA call made by upload() or
    // run_query() pays for it (a few hundred milliseconds)
    static void initialize_device();

private:
    void free_columns();
    void free_query_buffers();

    std::uint32_t *d_series_ids = nullptr;
    std::uint32_t *d_field_ids = nullptr;
    double *d_field_values = nullptr;
    std::uint64_t *d_timestamps = nullptr;
    size_t row_count = 0;

    std::uint8_t *d_series_filter = nullptr;
    size_t series_filter_capacity = 0;
    AggState *d_block_results = nullptr;
    size_t block_results_capacity = 0;
    std::vector<AggState> block_results;

    int multiprocessor_count = 0;
};

}
