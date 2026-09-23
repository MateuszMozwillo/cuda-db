#pragma once

#include <string_view>
#include <array>

namespace db {

constexpr unsigned int MAX_TAG_COUNT = 32;
constexpr unsigned int MAX_FIELD_COUNT = 128;

struct KeyValuePair {
    std::string_view key;
    std::string_view value;
};

struct DataPoint {
    std::string_view dataset;
    std::array<KeyValuePair, MAX_TAG_COUNT> tags;
    unsigned int tag_count = 0;
    std::array<KeyValuePair, MAX_FIELD_COUNT> fields;
    unsigned int field_count = 0;
    std::string_view timestamp;

    void clear() {
        tag_count = 0;
        field_count = 0;
        dataset = {};
        timestamp = {};
    }
};

enum ParserState {
    MEASUREMENT,
    TAG_KEY,
    TAG_VALUE,
    DATA_KEY,
    DATA_VALUE,
    TIMESTAMP,
    ERROR
};

bool parse_line(const char* input, DataPoint& result);
}
