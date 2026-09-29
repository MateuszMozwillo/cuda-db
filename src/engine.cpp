#include "db/engine.hpp"

#include "db/cpu_executor.hpp"

namespace db {

MemTable &Engine::get_mem_table(std::string_view dataset) {
    auto res = dataset_dict.find(dataset);
    if (res != dataset_dict.end()) {
        return mem_tables[res->second];
    }

    std::string key(dataset);
    dataset_dict.emplace(std::move(key), mem_tables.size());
    return mem_tables.emplace_back();
}

bool Engine::insert(const DataPoint &dp) {
    PreparedDp pd;
    bool res = MemTable::prepare(dp, pd);
    if (res == false) {
        return false;
    }
    get_mem_table(dp.dataset).commit(dp, pd);
    return true;
}

std::optional<double> Engine::query(const Query &query) const {
    const MemTable *mem_table = find_mem_table(query.dataset);
    if (mem_table == nullptr) {
        return finalize(AggState{}, query.operation_type);
    }

    ResolvedQuery resolved;
    if (resolve_query(*mem_table, query, resolved) == ResolveResult::NO_MATCH) {
        return finalize(AggState{}, query.operation_type);
    }

    AggState state = handle_cpu_query(*mem_table, resolved);
    return finalize(state, query.operation_type);
}

void Engine::print_mem_tables() {
    for (const auto &[dataset, idx] : dataset_dict) {
        std::cout << "[" << dataset << "]\n";
        mem_tables[idx].print_mem_table_columns();
    }
}

}
