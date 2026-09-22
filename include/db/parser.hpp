#pragma once

#include <string_view>
#include <charconv>

enum ParserState {
    MEASUREMENT,
    TAG_KEY,
    TAG_VALUE,
    DATA_KEY,
    DATA_VALUE,
    TIMESTAMP,
    ERROR
};

const unsigned int MAX_TAG_COUNT = 5;
const unsigned int MAX_FIELD_COUNT = 5;

struct KeyValuePair {
    std::string_view key;
    std::string_view value;
};
bool parse_line(const char* input, 
                std::string_view &measurement, 
                KeyValuePair tags[MAX_TAG_COUNT], 
                unsigned int &tag_count, 
                std::string_view &timestamp, 
                KeyValuePair fields[MAX_FIELD_COUNT],
                unsigned int &field_count);
