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
};

}
