#pragma once

#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <vector>

#include "db/mem_table.hpp"
#include "db/query.hpp"

namespace db {

class Engine {
private:
    std::unordered_map<std::string, size_t, StringHash, std::equal_to<>> dataset_dict;
    std::vector<MemTable> mem_tables;

    MemTable &get_mem_table(std::string_view dataset);
public:
    bool insert(const DataPoint &dp);
    std::optional<double> query(const Query &query) const;
    void print_mem_tables();

    size_t dataset_count() const { return mem_tables.size(); }
    const MemTable *find_mem_table(std::string_view dataset) const {
        auto res = dataset_dict.find(dataset);
        return res != dataset_dict.end() ? &mem_tables[res->second] : nullptr;
    }
};

}
