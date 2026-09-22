#include <iostream>
#include <string_view>
#include <charconv>

#include "db/parser.hpp"

int main() {
    const char* input = "sensor,location=Nowy-Jork temperature=80.5,pressure=1024.1";

    db::DataPoint point;
    
    if (!parse_line(input, point)) {
        std::cerr << "PARSING ERROR\n";
        return -1;
    }
    
    std::cout << "Measurement: [" << point.measurement << "]\n";
    std::cout << "--- TAGS ---\n";
    for(unsigned int i = 0; i < point.tag_count; i++) {
        std::cout << "[" << point.tags[i].key << "] : [" << point.tags[i].value << "]\n";
    }
    std::cout << "--- DATA ---\n";
    for(unsigned int i = 0; i < point.field_count; i++) {
        std::cout << "[" << point.fields[i].key << "] : [" << point.fields[i].value << "]\n";
    }
    std::cout << "--- TIME ---\n";
    std::cout << "Timestamp: [" << (point.timestamp.empty() ? "NONE PROVIDED" : point.timestamp) << "]\n";

    return 0;
}
