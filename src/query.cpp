#include "db/query.hpp"

namespace db {

std::optional<double> finalize(const AggState &state, OperationType operation_type) {
    if (operation_type == OperationType::COUNT) {
        return static_cast<double>(state.count);
    }

    if (state.count == 0) {
        return std::nullopt;
    }

    switch (operation_type) {
        case OperationType::MIN:
            return state.min;
        case OperationType::MAX:
            return state.max;
        case OperationType::AVG:
            return state.sum / static_cast<double>(state.count);
        case OperationType::COUNT:
            break;
    }

    return std::nullopt;
}

ResolveResult resolve_query(const MemTable &mem_table, const Query &query, ResolvedQuery &result) {
    result.operation_type = query.operation_type;

    auto field_id = mem_table.find_field_id(query.field);
    if (!field_id) return ResolveResult::NO_MATCH;
    result.field_id = field_id.value();

    if (query.from >= query.to) return ResolveResult::NO_MATCH;
    
    result.from = query.from;
    result.to = query.to;

    if (query.tags.empty()) {
        result.series_filter.clear();
        result.has_series_filter = false;
        return ResolveResult::OK;
    }

    std::vector<uint64_t> intersection_aux;
    std::vector<uint64_t> matching = mem_table.series_for_tag(query.tags[0]);
    for (size_t i = 1; i < query.tags.size(); ++i) {
        intersection_aux.clear();
        const auto &crnt_tag_series = mem_table.series_for_tag(query.tags[i]);
        std::set_intersection(matching.begin(), matching.end(),
                              crnt_tag_series.begin(), crnt_tag_series.end(),
                              std::back_inserter(intersection_aux)
        );
        intersection_aux.swap(matching);
    }

    if (matching.empty()) return ResolveResult::NO_MATCH;
    
    result.has_series_filter = true;
    result.series_filter.assign(mem_table.series_count() + 1, 0);
    for (const auto &series_id : matching) {
        result.series_filter[series_id] = 1;
    }

    return ResolveResult::OK;
}

}
