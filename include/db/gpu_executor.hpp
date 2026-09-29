#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/compression.hpp"
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
    size_t uploaded_bytes() const { return uploaded_byte_count; }

    double compression_seconds() const { return last_compression_seconds; }
    double transfer_seconds() const { return last_transfer_seconds; }

    static bool device_available();

private:
    struct DevicePackedColumn {
        PackedBlock *blocks = nullptr;
        std::uint64_t *words = nullptr;

        PackedColumnView view() const { return {blocks, words}; }
    };

    void free_columns();
    void free_query_buffers();
    void upload_packed(DevicePackedColumn &d_column, const PackedColumn &column);

    DevicePackedColumn d_series_ids;
    DevicePackedColumn d_field_ids;
    DevicePackedColumn d_timestamps;
    double *d_field_values = nullptr;
    size_t row_count = 0;
    size_t uploaded_byte_count = 0;
    double last_compression_seconds = 0.0;
    double last_transfer_seconds = 0.0;

    std::uint8_t *d_series_filter = nullptr;
    size_t series_filter_capacity = 0;
    AggState *d_block_results = nullptr;
    size_t block_results_capacity = 0;
    std::vector<AggState> block_results;

    int multiprocessor_count = 0;
};

}
