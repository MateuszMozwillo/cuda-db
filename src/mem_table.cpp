#include "db/mem_table.hpp"

namespace db {

std::pair<bool, u_int64_t> MemTable::get_series_id(std::string_view tags) {

    auto res = series_id_dict.find(tags);
    if (res != series_id_dict.end()) {
        return std::make_pair(true, res->second);
    }

    std::string key(tags);
    series_id_dict.emplace(std::move(key), next_series_id);
    return std::make_pair(false, next_series_id++);
}

u_int32_t MemTable::get_field_id(std::string_view field_name) {
    auto res = field_id_dict.find(field_name);
    if (res != field_id_dict.end()) {
        return res->second;
    }   

    std::string key(field_name);
    field_id_dict.emplace(std::move(key), next_field_id);
    return next_field_id++;

}

uint32_t MemTable::get_tag_id(std::string_view tag) {
    auto res = tag_id_dict.find(tag);
    if (res != tag_id_dict.end()) {
        return res->second;
    }   
    tag_to_series.emplace_back();
    std::string key(tag);
    tag_id_dict.emplace(std::move(key), next_tag_id);
    return next_tag_id++;
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

void MemTable::commit(const db::DataPoint &dp, const db::PreparedDp &to_commit) {
    auto [found, series_id] = get_series_id(to_commit.tags);
    if (found == false) {
        for (size_t i = 0; i < dp.tag_count; ++i) {
            std::string_view tag(dp.tags[i].key.data(),
                  dp.tags[i].value.data() + dp.tags[i].value.size() - dp.tags[i].key.data());
            tag_to_series[get_tag_id(tag)].push_back(series_id);
        }
    }

    for (size_t i = 0; i < dp.field_count; ++i) {
        col_series_id.push_back(series_id);
        col_field_id.push_back(get_field_id(dp.fields[i].key));
        col_field.push_back(to_commit.parsed_fields[i]);
        col_timestamp.push_back(to_commit.timestamp);
    }
}

bool MemTable::prepare(const db::DataPoint &dp, db::PreparedDp &result) {
    u_int64_t timestamp_as_int = 0;

    if (dp.timestamp.empty()) {
        auto now = std::chrono::system_clock::now();
        timestamp_as_int = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
    } else {
        auto [ts_ptr, ts_ec] = std::from_chars(dp.timestamp.data(), dp.timestamp.data() + dp.timestamp.size(), timestamp_as_int);

        if (ts_ec != std::errc() || ts_ptr != dp.timestamp.data() + dp.timestamp.size()) {
            return false;
        }
    }

    std::array<double, MAX_FIELD_COUNT> parsed_fields;

    for (size_t i = 0; i < dp.field_count; ++i) {
        const auto &field = dp.fields[i];

        double field_value_as_double = 0.0;
        auto [val_ptr, val_ec] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), field_value_as_double);

        if (val_ec != std::errc() || val_ptr != field.value.data() + field.value.size()) {
            return false; 
        }

        parsed_fields[i] = field_value_as_double;
    }

    result.timestamp = timestamp_as_int;
    result.tags = get_tags(dp);

    for (size_t i = 0; i < dp.field_count; ++i) {
        result.parsed_fields[i] = parsed_fields[i];
    }

    return true;
}

const std::vector<u_int64_t> &MemTable::series_for_tag(std::string_view tag) const {
    static const std::vector<u_int64_t> empty;

    auto res = tag_id_dict.find(tag);
    if (res == tag_id_dict.end()) {
        return empty;
    }
    return tag_to_series[res->second];
}

void MemTable::print_mem_table_columns() {
    for (int i = 0; i < col_field.size(); ++i) {
        std::cout << "[" << col_series_id[i] << ", " << static_cast<int>(col_field_id[i]) << ", " << col_field[i] << ", " << col_timestamp[i] << "]\n";
    }
}

}
