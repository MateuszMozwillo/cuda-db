#pragma once

#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <vector>

#include "db/mem_table.hpp"

namespace db {

class Engine {
private:
    std::unordered_map<std::string, size_t, StringHash, std::equal_to<>> dataset_dict;
    std::vector<db::MemTable> mem_tables;

    db::MemTable &get_mem_table(std::string_view dataset);
public:
    bool insert(const db::DataPoint &dp);
    void print_mem_tables();

    size_t dataset_count() const { return mem_tables.size(); }
    const db::MemTable *find_mem_table(std::string_view dataset) const {
        auto res = dataset_dict.find(dataset);
        return res != dataset_dict.end() ? &mem_tables[res->second] : nullptr;
    }
};

}
