#include <iostream>
#include <string_view>
#include <charconv>

#include "db/parser.hpp"

int main() {
    const char* input = "sensor,location=Nowy-Jork temperature=80.5,pressure=1024.1";

    std::string_view measurement;
    KeyValuePair tags[MAX_TAG_COUNT];
    unsigned int tag_count = 0;
    
    KeyValuePair fields[MAX_FIELD_COUNT];
    unsigned int field_count = 0;
    
    std::string_view timestamp;
    
    if (!parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count)) {
        std::cerr << "PARSING ERROR\n";
        return -1;
    }
    
    std::cout << "Measurement: [" << measurement << "]\n";
    std::cout << "--- TAGS ---\n";
    for(unsigned int i = 0; i < tag_count; i++) {
        std::cout << "[" << tags[i].key << "] : [" << tags[i].value << "]\n";
    }
    std::cout << "--- DATA ---\n";
    for(unsigned int i = 0; i < field_count; i++) {
        std::cout << "[" << fields[i].key << "] : [" << fields[i].value << "]\n";
    }
    std::cout << "--- TIME ---\n";
    std::cout << "Timestamp: [" << (timestamp.empty() ? "NONE PROVIDED" : timestamp) << "]\n";

    return 0;
}
