#pragma once

#include <unordered_map>
#include <string>
#include <string_view>
#include <functional>
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
    std::unordered_map<std::string, u_int64_t, StringHash, std::equal_to<>> id_dict;
    u_int64_t next_dbid = 1;

    u_int64_t get_dbid(std::string_view dataset_and_tags);
    std::string_view get_dataset_and_tags(const db::DataPoint &dp);
public:
    void insert(const db::DataPoint &dp);
};
}
