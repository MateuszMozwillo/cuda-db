#include "db/cpu_executor.hpp"

#include <algorithm>
#include <thread>
#include <vector>

namespace db {

namespace {

constexpr size_t MIN_ROWS_PER_THREAD = 50'000;

AggState scan_range(const MemTable &mem_table, const ResolvedQuery &query, size_t begin, size_t end) {
    const auto &series_ids = mem_table.series_ids();
    const auto &field_ids = mem_table.field_ids();
    const auto &field_values = mem_table.field_values();
    const auto &timestamps = mem_table.timestamps();

    AggState result;

    for (size_t i = begin; i < end; ++i) {
        if (field_ids[i] != query.field_id) continue;
        if (timestamps[i] < query.from || timestamps[i] >= query.to) continue;
        if (query.has_series_filter && query.series_filter[series_ids[i]] == 0) continue;
        result.update(field_values[i]);
    }

    return result;
}

size_t pick_thread_count(size_t row_count, unsigned requested) {
    if (requested == 0) {
        size_t hardware = std::max(1u, std::thread::hardware_concurrency());
        size_t by_rows = std::max<size_t>(1, row_count / MIN_ROWS_PER_THREAD);
        return std::min(hardware, by_rows);
    }
    return std::clamp<size_t>(requested, 1, std::max<size_t>(1, row_count));
}

}

AggState handle_cpu_query(const MemTable &mem_table, const ResolvedQuery &query, unsigned thread_count) {
    const size_t row_count = mem_table.row_count();
    const size_t threads_used = pick_thread_count(row_count, thread_count);

    if (threads_used == 1) {
        return scan_range(mem_table, query, 0, row_count);
    }

    std::vector<AggState> partial(threads_used);
    std::vector<std::thread> threads;
    threads.reserve(threads_used - 1);

    for (size_t t = 1; t < threads_used; ++t) {
        size_t begin = row_count * t / threads_used;
        size_t end = row_count * (t + 1) / threads_used;
        threads.emplace_back([&mem_table, &query, &partial, t, begin, end]() {
            partial[t] = scan_range(mem_table, query, begin, end);
        });
    }

    partial[0] = scan_range(mem_table, query, 0, row_count / threads_used);

    for (auto &thread : threads) {
        thread.join();
    }

    AggState result;
    for (const auto &state : partial) {
        result.merge(state);
    }
    return result;
}

}
