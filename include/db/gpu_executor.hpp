#pragma once

#include <cstddef>
#include <cstdint>

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
    AggState query(const ResolvedQuery &query);

    size_t uploaded_rows() const { return row_count; }

private:
    void free_columns();

    std::uint32_t *d_series_ids = nullptr;
    std::uint32_t *d_field_ids = nullptr;
    double *d_field_values = nullptr;
    std::uint64_t *d_timestamps = nullptr;
    size_t row_count = 0;
};

}
