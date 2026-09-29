#include "db/cpu_executor.hpp"

namespace db {
    
AggState handle_cpu_query(const MemTable &mem_table, const ResolvedQuery &query) {
    const auto &series_ids = mem_table.series_ids();
    const auto &field_ids = mem_table.field_ids();
    const auto &field_values = mem_table.field_values();
    const auto &timestamps = mem_table.timestamps();
    
    AggState result;
    
    for (size_t i = 0; i < mem_table.row_count(); ++i) {
        if (field_ids[i] != query.field_id) continue;
        if (timestamps[i] < query.from || timestamps[i] >= query.to) continue;
        if (query.has_series_filter && query.series_filter[series_ids[i]] == 0) continue;
        result.update(field_values[i]);
    }

    return result;
}

}
