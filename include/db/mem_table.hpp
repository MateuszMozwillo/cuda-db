#pragma once

#include <charconv>
#include <cstdint>
#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>
#include <utility>
#include <iostream>

#include "db/parser.hpp"

namespace db {

struct StringHash {
    using is_transparent = void; 
    size_t operator()(std::string_view txt) const {
        return std::hash<std::string_view>{}(txt);
    }
};

struct PreparedDp {
    std::string_view tags;
    std::array<double, MAX_FIELD_COUNT> parsed_fields;
    std::uint64_t timestamp;
};


class MemTable {
private:
    std::unordered_map<std::string, std::uint64_t, StringHash, std::equal_to<>> series_id_dict;
    std::uint64_t next_series_id = 1;

    std::unordered_map<std::string, std::uint32_t, StringHash, std::equal_to<>> field_id_dict;
    std::uint32_t next_field_id = 1;

    std::unordered_map<std::string, std::uint32_t, StringHash, std::equal_to<>> tag_id_dict; 
    std::uint32_t next_tag_id = 0;
    std::vector<std::vector<std::uint64_t>> tag_to_series;

    std::pair<bool, std::uint64_t> get_series_id(std::string_view tags);
    static std::string_view get_tags(const DataPoint &dp);
    std::uint32_t get_tag_id(std::string_view tag);

    std::uint32_t get_field_id(std::string_view field_name);

    std::vector<std::uint64_t> col_series_id;
    std::vector<std::uint32_t> col_field_id;
    std::vector<double> col_field;
    std::vector<std::uint64_t> col_timestamp;

public:
    static bool prepare(const DataPoint &dp, PreparedDp &result);
    void commit(const DataPoint &dp, const PreparedDp &pd);
    void print_mem_table_columns();

    size_t row_count() const { return col_field.size(); }
    const std::vector<std::uint64_t> &series_ids() const { return col_series_id; }
    const std::vector<std::uint32_t> &field_ids() const { return col_field_id; }
    const std::vector<double> &field_values() const { return col_field; }
    const std::vector<std::uint64_t> &timestamps() const { return col_timestamp; }
    const std::vector<std::uint64_t> &series_for_tag(std::string_view tag) const;
};
}
