#include <iostream>
#include <string_view>
#include <charconv>
#include <unordered_map>

#include "db/parser.hpp"
#include "db/mem_table.hpp"

void print_data_point(const db::DataPoint &point) {
    std::cout << "Dataset: [" << point.dataset << "]\n";
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
}

int main() {
    const char* input = "sensor,location=Nowy-Jork,versions=3.4 temperature=80.5,pressure=1024.1";

    db::DataPoint point;
    
    if (!parse_line(input, point)) {
        std::cerr << "PARSING ERROR\n";
        return -1;
    }

    print_data_point(point);
    

    return 0;
}
