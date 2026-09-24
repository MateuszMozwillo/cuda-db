#include <iostream>
#include <string_view>
#include <charconv>
#include <unordered_map>

#include "db/parser.hpp"
#include "db/engine.hpp"

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

    db::Engine engine;

    const char* inputs[] = {
        "sensor,location=Nowy-Jork,versions=3.4 temperature=80.5,pressure=1024.1",
        "sensor,location=Krakow,versions=3.4 temperature=20.1,pressure=1013.2",
        "cpu,host=server01 usage=42.5",
    };

    for (const char* input : inputs) {
        db::DataPoint point;
        if (!parse_line(input, point)) {
            std::cerr << "PARSING ERROR\n";
            return -1;
        }
        engine.insert(point);
    }

    engine.print_mem_tables();

    return 0;
}
