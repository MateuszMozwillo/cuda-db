#include "db/parser.hpp"

namespace db {

bool parse_line(const char* input, DataPoint& result) {
    result.clear();

    ParserState ps = MEASUREMENT;
    const char* start_ptr = input; 
    const char* current = input;
    bool in_quotes = false; 

    result.timestamp = std::string_view(current, 0);

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
                    result.measurement = std::string_view(start_ptr, current - start_ptr);
                    if (result.measurement.empty()) { ps = ERROR; break; }
                    start_ptr = current + 1;
                    ps = TAG_KEY;
                } else if (*current == ' ') { 
                    result.measurement = std::string_view(start_ptr, current - start_ptr);
                    if (result.measurement.empty()) { ps = ERROR; break; }
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = DATA_KEY;
                }
                break;

            case TAG_KEY:
                if (*current == '=') {
                    if (result.tag_count >= MAX_TAG_COUNT) { ps = ERROR; break; }
                    result.tags[result.tag_count].key = std::string_view(start_ptr, current - start_ptr);
                    if (result.tags[result.tag_count].key.empty()) { ps = ERROR; break; }
                    start_ptr = current + 1;
                    ps = TAG_VALUE;
                }
                break;

            case TAG_VALUE:
                if (*current == ',') {
                    result.tags[result.tag_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.tags[result.tag_count].value.empty()) { ps = ERROR; break; }
                    result.tag_count++;
                    start_ptr = current + 1;
                    ps = TAG_KEY;
                } else if (*current == ' ') {
                    result.tags[result.tag_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.tags[result.tag_count].value.empty()) { ps = ERROR; break; }
                    result.tag_count++;
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = DATA_KEY;
                }
                break;

            case DATA_KEY:
                if (*current == '=') {
                    if (result.field_count >= MAX_FIELD_COUNT) { ps = ERROR; break; }
                    result.fields[result.field_count].key = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].key.empty()) { ps = ERROR; break; }
                    start_ptr = current + 1;
                    ps = DATA_VALUE;
                }
                break;

            case DATA_VALUE:
                if (*current == '"') {
                    in_quotes = !in_quotes;
                } 
                else if (*current == ',' && !in_quotes) {
                    result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].value.empty()) { ps = ERROR; break; }
                    result.field_count++;
                    start_ptr = current + 1;
                    ps = DATA_KEY;
                } else if (*current == ' ' && !in_quotes) {
                    result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].value.empty()) { ps = ERROR; break; }
                    result.field_count++;
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = TIMESTAMP;
                } else if (*current == '\n' || *current == '\r') {
                    if (in_quotes) { 
                        ps = ERROR; 
                        break; 
                    }
                    
                    result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].value.empty()) { ps = ERROR; break; }
                    result.field_count++;

                    result.timestamp = std::string_view(current, 0); 
                    ps = TIMESTAMP;
                    goto end_loop;
                }
                break;

            case TIMESTAMP:
                if (*current == '\n' || *current == '\r') {
                    result.timestamp = std::string_view(start_ptr, current - start_ptr);
                    while (!result.timestamp.empty() && result.timestamp.back() == ' ') {
                        result.timestamp.remove_suffix(1);
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
    if (in_quotes) {
        ps = ERROR;
    }

    if (*current == '\0' && ps != ERROR) {
        if (ps == DATA_VALUE && current > start_ptr) {
            result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
            if (!result.fields[result.field_count].value.empty()) {
                result.field_count++;
                result.timestamp = std::string_view(current, 0);
                ps = TIMESTAMP; 
            } else {
                ps = ERROR;
            }
        } else if (ps == TIMESTAMP && current > start_ptr) {
            result.timestamp = std::string_view(start_ptr, current - start_ptr);
            while (!result.timestamp.empty() && result.timestamp.back() == ' ') {
                result.timestamp.remove_suffix(1);
            }
        }
    }

    if (ps == ERROR || ps != TIMESTAMP || result.field_count == 0) {
        return false;
    }

    return true;
}
}
