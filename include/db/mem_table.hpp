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

    std::unordered_map<std::string, u_int32_t, StringHash, std::equal_to<>> field_id_dict;
    u_int32_t next_field_id = 1;

    u_int64_t get_series_id(std::string_view tags);
    std::string_view get_tags(const db::DataPoint &dp);

    u_int32_t get_field_id(std::string_view field_name);

    std::vector<u_int64_t> col_series_id;
    std::vector<u_int32_t> col_field_id;
    std::vector<double> col_field;
    std::vector<u_int64_t> col_timestamp;

public:
    bool insert(const db::DataPoint &dp);
    void print_mem_table_columns();

    size_t row_count() const { return col_field.size(); }
    const std::vector<u_int64_t> &series_ids() const { return col_series_id; }
    const std::vector<u_int32_t> &field_ids() const { return col_field_id; }
    const std::vector<double> &field_values() const { return col_field; }
    const std::vector<u_int64_t> &timestamps() const { return col_timestamp; }
};
}
