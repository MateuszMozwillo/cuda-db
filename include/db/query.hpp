#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <limits>
#include <cmath>
#include <optional>

#include "db/host_device.hpp"
#include "db/mem_table.hpp"

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

    DB_HOST_DEVICE void update(double value);
    DB_HOST_DEVICE void merge(const AggState &other);
};

DB_HOST_DEVICE inline void AggState::update(double value) {
    min = fmin(min, value);
    max = fmax(max, value);
    sum += value;
    ++count;
}

DB_HOST_DEVICE inline void AggState::merge(const AggState &other) {
    min = fmin(min, other.min);
    max = fmax(max, other.max);
    sum += other.sum;
    count += other.count;
}

std::optional<double> finalize(const AggState &state, OperationType operation_type);

class MemTable;

struct Query {
    std::string dataset;
    std::vector<std::string> tags;
    std::string field;
    std::uint64_t from = 0;
    std::uint64_t to = std::numeric_limits<std::uint64_t>::max();
    OperationType operation_type;
};

struct ResolvedQuery {
    std::uint32_t field_id = 0;
    std::uint64_t from = 0;
    std::uint64_t to = std::numeric_limits<std::uint64_t>::max();

    bool has_series_filter = false;
    std::vector<std::uint8_t> series_filter;

    OperationType operation_type;
};

enum class ResolveResult {
    OK,
    NO_MATCH
};

ResolveResult resolve_query(const MemTable &mem_table, const Query &query, ResolvedQuery &result);

}
