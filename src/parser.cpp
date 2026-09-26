#include "db/parser.hpp"

namespace db {

bool parse_line(const char* input, DataPoint& result) {
    result.clear();

    ParserState ps = ParserState::MEASUREMENT;
    const char* start_ptr = input; 
    const char* current = input;
    bool in_quotes = false; 

    result.timestamp = std::string_view(current, 0);

    while (*current != '\0' && ps != ParserState::ERROR) {

        if (*current == '\\') {
            if (*(current + 1) != '\0') {
                current += 2; 
                continue;    
            } else {
                ps = ParserState::ERROR;   
                break;
            }
        }

        switch (ps) {
            case ParserState::MEASUREMENT:
                if (*current == ',') {
                    result.dataset = std::string_view(start_ptr, current - start_ptr);
                    if (result.dataset.empty()) { ps = ParserState::ERROR; break; }
                    start_ptr = current + 1;
                    ps = ParserState::TAG_KEY;
                } else if (*current == ' ') { 
                    result.dataset = std::string_view(start_ptr, current - start_ptr);
                    if (result.dataset.empty()) { ps = ParserState::ERROR; break; }
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = ParserState::DATA_KEY;
                }
                break;

            case ParserState::TAG_KEY:
                if (*current == '=') {
                    if (result.tag_count >= MAX_TAG_COUNT) { ps = ParserState::ERROR; break; }
                    result.tags[result.tag_count].key = std::string_view(start_ptr, current - start_ptr);
                    if (result.tags[result.tag_count].key.empty()) { ps = ParserState::ERROR; break; }
                    start_ptr = current + 1;
                    ps = ParserState::TAG_VALUE;
                } else if (*current == ' ' || *current == ',') {
                    ps = ParserState::ERROR;
                }
                break;

            case ParserState::TAG_VALUE:
                if (*current == ',') {
                    result.tags[result.tag_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.tags[result.tag_count].value.empty()) { ps = ParserState::ERROR; break; }
                    result.tag_count++;
                    start_ptr = current + 1;
                    ps = ParserState::TAG_KEY;
                } else if (*current == ' ') {
                    result.tags[result.tag_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.tags[result.tag_count].value.empty()) { ps = ParserState::ERROR; break; }
                    result.tag_count++;
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = ParserState::DATA_KEY;
                } else if (*current == '=') {
                    ps = ParserState::ERROR;
                }
                break;

            case ParserState::DATA_KEY:
                if (*current == '=') {
                    if (result.field_count >= MAX_FIELD_COUNT) { ps = ParserState::ERROR; break; }
                    result.fields[result.field_count].key = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].key.empty()) { ps = ParserState::ERROR; break; }
                    start_ptr = current + 1;
                    ps = ParserState::DATA_VALUE;
                } else if (*current == ' ' || *current == ',') {
                    ps = ParserState::ERROR;
                }
                break;

            case ParserState::DATA_VALUE:
                if (*current == '"') {
                    if (!in_quotes) {
                        if (current != start_ptr) { ps = ParserState::ERROR; break; }
                        in_quotes = true;
                    } else {
                        const char next = *(current + 1);
                        if (next != ',' && next != ' ' && next != '\n' && next != '\r' && next != '\0') {
                            ps = ParserState::ERROR;
                            break;
                        }
                        in_quotes = false;
                    }
                }
                else if (*current == ',' && !in_quotes) {
                    result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].value.empty()) { ps = ParserState::ERROR; break; }
                    result.field_count++;
                    start_ptr = current + 1;
                    ps = ParserState::DATA_KEY;
                } else if (*current == ' ' && !in_quotes) {
                    result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].value.empty()) { ps = ParserState::ERROR; break; }
                    result.field_count++;
                    
                    while (*(current + 1) == ' ') current++;
                    
                    start_ptr = current + 1;
                    ps = ParserState::TIMESTAMP;
                } else if (*current == '\n' || *current == '\r') {
                    if (in_quotes) { 
                        ps = ParserState::ERROR; 
                        break; 
                    }
                    
                    result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
                    if (result.fields[result.field_count].value.empty()) { ps = ParserState::ERROR; break; }
                    result.field_count++;

                    result.timestamp = std::string_view(current, 0); 
                    ps = ParserState::TIMESTAMP;
                    goto end_loop;
                }
                break;

            case ParserState::TIMESTAMP:
                if (*current == '\n' || *current == '\r') {
                    result.timestamp = std::string_view(start_ptr, current - start_ptr);
                    while (!result.timestamp.empty() && result.timestamp.back() == ' ') {
                        result.timestamp.remove_suffix(1);
                    }
                    goto end_loop;
                }
                break;

            case ParserState::ERROR:
                break;
        }
        current++;
    }

end_loop:
    if (in_quotes) {
        ps = ParserState::ERROR;
    }

    if (*current == '\0' && ps != ParserState::ERROR) {
        if (ps == ParserState::DATA_VALUE && current > start_ptr) {
            result.fields[result.field_count].value = std::string_view(start_ptr, current - start_ptr);
            if (!result.fields[result.field_count].value.empty()) {
                result.field_count++;
                result.timestamp = std::string_view(current, 0);
                ps = ParserState::TIMESTAMP; 
            } else {
                ps = ParserState::ERROR;
            }
        } else if (ps == ParserState::TIMESTAMP && current > start_ptr) {
            result.timestamp = std::string_view(start_ptr, current - start_ptr);
            while (!result.timestamp.empty() && result.timestamp.back() == ' ') {
                result.timestamp.remove_suffix(1);
            }
        }
    }

    if (ps == ParserState::ERROR || ps != ParserState::TIMESTAMP || result.field_count == 0) {
        return false;
    }

    return true;
}
}
