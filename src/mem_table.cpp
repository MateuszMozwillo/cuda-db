#include "db/mem_table.hpp"

namespace db {

u_int64_t MemTable::get_dbid(std::string_view dataset_and_tags) {

    auto res = id_dict.find(dataset_and_tags);
    if (res != id_dict.end()) {
        return res->second;
    }   

    std::string key(dataset_and_tags);
    id_dict.emplace(std::move(key), next_dbid);
    return next_dbid++;
}

std::string_view MemTable::get_dataset_and_tags(const db::DataPoint &dp) {
    std::string_view dataset_and_tags;

    if (dp.tag_count > 0) {
        const char* start_ptr = dp.dataset.data();
        
        const auto& last_tag = dp.tags[dp.tag_count - 1];
        const char* end_ptr = last_tag.value.data() + last_tag.value.size();
        
        dataset_and_tags = std::string_view(start_ptr, end_ptr - start_ptr);
    } else {
        dataset_and_tags = dp.dataset;
    }
    return dataset_and_tags;
}

void MemTable::insert(const db::DataPoint &dp) {
    const std::string_view dataset_and_tags = get_dataset_and_tags(dp);
    const u_int64_t db_id = get_dbid(dataset_and_tags);
}

}
