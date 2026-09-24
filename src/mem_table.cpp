#include "db/mem_table.hpp"

namespace db {

u_int64_t MemTable::get_series_id(std::string_view tags) {

    auto res = series_id_dict.find(tags);
    if (res != series_id_dict.end()) {
        return res->second;
    }

    std::string key(tags);
    series_id_dict.emplace(std::move(key), next_series_id);
    return next_series_id++;
}

u_int64_t MemTable::get_field_id(std::string_view field_name) {
    auto res = field_id_dict.find(field_name);
    if (res != field_id_dict.end()) {
        return res->second;
    }   

    std::string key(field_name);
    field_id_dict.emplace(std::move(key), next_field_id);
    return next_field_id++;

}

std::string_view MemTable::get_tags(const db::DataPoint &dp) {
    if (dp.tag_count == 0) {
        return {};
    }

    const char* start_ptr = dp.tags[0].key.data();

    const auto& last_tag = dp.tags[dp.tag_count - 1];
    const char* end_ptr = last_tag.value.data() + last_tag.value.size();

    return std::string_view(start_ptr, end_ptr - start_ptr);
}

bool MemTable::insert(const db::DataPoint &dp) {
    const std::string_view tags = get_tags(dp);
    const u_int64_t series_id = get_series_id(tags);

    u_int64_t timestamp_as_int = 0;

    if (dp.timestamp.empty()) {
        auto now = std::chrono::system_clock::now();
        timestamp_as_int = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
    } else {
        auto [ts_ptr, ts_ec] = std::from_chars(dp.timestamp.data(), dp.timestamp.data() + dp.timestamp.size(), timestamp_as_int);

        if (ts_ec != std::errc()) {
            return false;
        }
    }

    for (size_t i = 0; i < dp.field_count; ++i) {
        const auto &field = dp.fields[i];
        u_int64_t field_id = get_field_id(field.key);

        double field_value_as_double = 0.0;
        auto [val_ptr, val_ec] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), field_value_as_double);

        if (val_ec != std::errc()) {
            continue; 
        }

        col_series_id.push_back(series_id);
        col_field_id.push_back(field_id);
        col_field.push_back(field_value_as_double);
        col_timestamp.push_back(timestamp_as_int);
    }

    return true;
}

void MemTable::print_mem_table_columns() {
    for (int i = 0; i < col_field.size(); ++i) {
        std::cout << "[" << col_series_id[i] << ", " << static_cast<int>(col_field_id[i]) << ", " << col_field[i] << ", " << col_timestamp[i] << "]\n";
    }
}

}
