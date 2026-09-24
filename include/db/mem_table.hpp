#pragma once

#include <charconv>
#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>
#include <iostream>

#include "db/parser.hpp"

namespace db {

struct StringHash {
    using is_transparent = void; 
    size_t operator()(std::string_view txt) const {
        return std::hash<std::string_view>{}(txt);
    }
};

class MemTable {
private:
    std::unordered_map<std::string, u_int64_t, StringHash, std::equal_to<>> series_id_dict;
    u_int64_t next_series_id = 1;

    std::unordered_map<std::string, u_int64_t, StringHash, std::equal_to<>> field_id_dict;
    u_int64_t next_field_id = 1;

    u_int64_t get_series_id(std::string_view tags);
    std::string_view get_tags(const db::DataPoint &dp);

    u_int64_t get_field_id(std::string_view field_name);

    std::vector<u_int64_t> col_series_id;
    std::vector<u_int8_t> col_field_id;
    std::vector<double> col_field;
    std::vector<u_int64_t> col_timestamp;

public:
    bool insert(const db::DataPoint &dp);
    void print_mem_table_columns();
};
}
