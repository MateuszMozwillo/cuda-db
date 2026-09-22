#include <iostream>
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
                unsigned int &field_count) 
{
    ParserState ps = MEASUREMENT;
    const char* start_ptr = input; 
    const char* current = input;
    
    tag_count = 0;
    field_count = 0;

    while (*current != '\0' && ps != ERROR) {
        
        if (*current == '\\') {
            if (*(current + 1) != '\0') {
                current += 2; 
                continue;    
            } else {
                ps = ERROR;   
                break;
            }
        }

        switch (ps) {
            case MEASUREMENT:
                if (*current == ',') {
                    measurement = std::string_view(start_ptr, current - start_ptr);
                    if (measurement.empty()) { ps = ERROR; break; }
                    start_ptr = current + 1;
                    ps = TAG_KEY;
                } else if (*current == ' ') { 
                    measurement = std::string_view(start_ptr, current - start_ptr);
                    if (measurement.empty()) { ps = ERROR; break; }
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = DATA_KEY;
                }
                break;

            case TAG_KEY:
                if (*current == '=') {
                    if (tag_count >= MAX_TAG_COUNT) { ps = ERROR; break; }
                    tags[tag_count].key = std::string_view(start_ptr, current - start_ptr);
                    if (tags[tag_count].key.empty()) { ps = ERROR; break; }
                    start_ptr = current + 1;
                    ps = TAG_VALUE;
                }
                break;

            case TAG_VALUE:
                if (*current == ',') {
                    tags[tag_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (tags[tag_count].value.empty()) { ps = ERROR; break; }
                    tag_count++;
                    start_ptr = current + 1;
                    ps = TAG_KEY;
                } else if (*current == ' ') {
                    tags[tag_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (tags[tag_count].value.empty()) { ps = ERROR; break; }
                    tag_count++;
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = DATA_KEY;
                }
                break;

            case DATA_KEY:
                if (*current == '=') {
                    if (field_count >= MAX_FIELD_COUNT) { ps = ERROR; break; }
                    fields[field_count].key = std::string_view(start_ptr, current - start_ptr);
                    if (fields[field_count].key.empty()) { ps = ERROR; break; }
                    start_ptr = current + 1;
                    ps = DATA_VALUE;
                }
                break;

            case DATA_VALUE:
                if (*current == ',') {
                    fields[field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (fields[field_count].value.empty()) { ps = ERROR; break; }
                    field_count++;
                    start_ptr = current + 1;
                    ps = DATA_KEY;
                } else if (*current == ' ') {
                    fields[field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (fields[field_count].value.empty()) { ps = ERROR; break; }
                    field_count++;
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = TIMESTAMP;
                } else if (*current == '\n' || *current == '\r') {
                    fields[field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (fields[field_count].value.empty()) { ps = ERROR; break; }
                    field_count++;
                    ps = TIMESTAMP; 
                    goto end_loop;
                }
                break;

            case TIMESTAMP:
                if (*current == '\n' || *current == '\r') {
                    timestamp = std::string_view(start_ptr, current - start_ptr);
                    while (!timestamp.empty() && timestamp.back() == ' ') {
                        timestamp.remove_suffix(1);
                    }
                    goto end_loop;
                }
                break;

            case ERROR:
                break;
        }
        current++;
    }

end_loop:
    if (ps == TIMESTAMP && timestamp.empty() && current > start_ptr) {
        timestamp = std::string_view(start_ptr, current - start_ptr);
        while (!timestamp.empty() && timestamp.back() == ' ') {
            timestamp.remove_suffix(1);
        }
    } else if (ps == DATA_VALUE && current > start_ptr) {
        fields[field_count].value = std::string_view(start_ptr, current - start_ptr);
        if (!fields[field_count].value.empty()) {
            field_count++;
            ps = TIMESTAMP; 
        } else {
            ps = ERROR;
        }
    }

    if (ps == ERROR || ps != TIMESTAMP || field_count == 0) {
        return false;
    }

    return true;
}

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
