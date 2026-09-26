#include "db/engine.hpp"

namespace db {

db::MemTable &Engine::get_mem_table(std::string_view dataset) {
    auto res = dataset_dict.find(dataset);
    if (res != dataset_dict.end()) {
        return mem_tables[res->second];
    }

    std::string key(dataset);
    dataset_dict.emplace(std::move(key), mem_tables.size());
    return mem_tables.emplace_back();
}

bool Engine::insert(const db::DataPoint &dp) {
    PreparedDp pd;
    bool res = MemTable::prepare(dp, pd);
    if (res == false) {
        return false;
    }
    get_mem_table(dp.dataset).commit(dp, pd);
    return true;
}

void Engine::print_mem_tables() {
    for (const auto &[dataset, idx] : dataset_dict) {
        std::cout << "[" << dataset << "]\n";
        mem_tables[idx].print_mem_table_columns();
    }
}

}
