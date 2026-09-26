#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <limits>

namespace db {

enum class OperationType {
    MIN,
    MAX,
    COUNT,
    AVG
};

struct AggState {
    std::uint64_t count = 0;
    double sum = 0.0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
};

struct Query {
    std::string dataset;
    std::vector<std::string> tags;
    std::string field;
    OperationType operation_type;
};

}
