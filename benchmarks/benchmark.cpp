/*
    Generates random line protocol data, loads it into the engine and measures
    query performance.
    usage: ./benchmark [line_count] [seed]
*/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "db/engine.hpp"
#ifdef GPU_DB_HAS_CUDA
#include "db/gpu_executor.hpp"
#endif
#include "db/parser.hpp"
#include "db/query.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t DEFAULT_LINE_COUNT = 5'000'000;
constexpr std::uint64_t DEFAULT_SEED = 42;
constexpr std::size_t CHUNK_LINES = 100'000;
constexpr std::uint64_t START_TIMESTAMP = 1'700'000'000'000'000'000ULL;
constexpr int QUERY_REPEATS = 50;

struct DatasetSpec {
    const char *name;
    std::vector<const char *> fields;
    double min_value;
    double max_value;
};

const std::vector<DatasetSpec> DATASETS = {
    {"cpu", {"usage_user", "usage_system", "usage_idle"}, 0.0, 100.0},
    {"memory", {"used_percent", "available_gb"}, 0.0, 64.0},
    {"disk", {"read_mb", "write_mb"}, 0.0, 500.0},
    {"sensor", {"temperature", "humidity", "pressure"}, -20.0, 1050.0},
};

const std::vector<const char *> ENVS = {"dev", "prod", "staging"};
const std::vector<const char *> REGIONS = {"ap-south", "eu-central", "eu-west", "us-east", "us-west"};
constexpr int HOST_COUNT = 100;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

class LineGenerator {
public:
    explicit LineGenerator(std::uint64_t seed)
        : rng(seed),
          dataset_dist(0, DATASETS.size() - 1),
          env_dist(0, ENVS.size() - 1),
          host_dist(0, HOST_COUNT - 1),
          ts_step_dist(1, 1000),
          value_dist(0.0, 1.0) {}

    void append_line(std::string &out) {
        const DatasetSpec &dataset = DATASETS[dataset_dist(rng)];
        int host = host_dist(rng);
        const char *env = ENVS[env_dist(rng)];
        const char *region = REGIONS[host % REGIONS.size()];

        timestamp += ts_step_dist(rng);

        char buf[64];
        out += dataset.name;
        out += ",env=";
        out += env;
        out += ",host=host_";
        out += std::to_string(host);
        out += ",region=";
        out += region;
        out += ' ';

        for (std::size_t i = 0; i < dataset.fields.size(); ++i) {
            double value = dataset.min_value + value_dist(rng) * (dataset.max_value - dataset.min_value);
            int len = std::snprintf(buf, sizeof(buf), "%.2f", value);

            if (i > 0) out += ',';
            out += dataset.fields[i];
            out += '=';
            out.append(buf, len);
        }

        out += ' ';
        out += std::to_string(timestamp);
        out += '\n';
    }

    std::uint64_t last_timestamp() const { return timestamp; }

private:
    std::mt19937_64 rng;
    std::uniform_int_distribution<std::size_t> dataset_dist;
    std::uniform_int_distribution<std::size_t> env_dist;
    std::uniform_int_distribution<int> host_dist;
    std::uniform_int_distribution<std::uint64_t> ts_step_dist;
    std::uniform_real_distribution<double> value_dist;
    std::uint64_t timestamp = START_TIMESTAMP;
};

struct NamedQuery {
    const char *description;
    db::Query query;
};

db::Query make_query(const char *dataset, const char *field, db::OperationType op,
                     std::vector<std::string> tags = {},
                     std::uint64_t from = 0,
                     std::uint64_t to = std::numeric_limits<std::uint64_t>::max()) {
    db::Query query;
    query.dataset = dataset;
    query.field = field;
    query.tags = std::move(tags);
    query.from = from;
    query.to = to;
    query.operation_type = op;
    return query;
}

const char *operation_name(db::OperationType op) {
    switch (op) {
        case db::OperationType::MIN: return "MIN";
        case db::OperationType::MAX: return "MAX";
        case db::OperationType::COUNT: return "COUNT";
        case db::OperationType::AVG: return "AVG";
    }
    return "?";
}

struct Timing {
    double best;
    double median;
    double average;
};

template <typename Fn>
Timing measure(Fn fn) {
    std::vector<double> times;
    times.reserve(QUERY_REPEATS);

    for (int i = 0; i < QUERY_REPEATS; ++i) {
        auto start = Clock::now();
        fn();
        times.push_back(seconds_since(start));
    }

    std::sort(times.begin(), times.end());
    double median = times.size() % 2 == 1
        ? times[times.size() / 2]
        : (times[times.size() / 2 - 1] + times[times.size() / 2]) / 2.0;
    double total = 0.0;
    for (double time : times) total += time;

    return {times.front(), median, total / times.size()};
}

std::string format_result(std::optional<double> result) {
    char text[32];
    if (result) {
        std::snprintf(text, sizeof(text), "%.3f", *result);
    } else {
        std::snprintf(text, sizeof(text), "no data");
    }
    return text;
}

}

int main(int argc, char *argv[]) {
    std::size_t line_count = DEFAULT_LINE_COUNT;
    std::uint64_t seed = DEFAULT_SEED;
    if (argc > 1) line_count = std::strtoull(argv[1], nullptr, 10);
    if (argc > 2) seed = std::strtoull(argv[2], nullptr, 10);

    std::printf("gpu-db benchmark: %zu lines, seed %llu\n\n", line_count, static_cast<unsigned long long>(seed));

    LineGenerator generator(seed);
    db::Engine engine;

    std::size_t bytes_total = 0;

    std::string chunk;
    std::vector<std::size_t> line_offsets;
    db::DataPoint point;

    for (std::size_t done = 0; done < line_count; ) {
        std::size_t lines = std::min(CHUNK_LINES, line_count - done);

        chunk.clear();
        line_offsets.clear();
        for (std::size_t i = 0; i < lines; ++i) {
            line_offsets.push_back(chunk.size());
            generator.append_line(chunk);
        }
        bytes_total += chunk.size();

        for (std::size_t offset : line_offsets) {
            if (db::parse_line(chunk.c_str() + offset, point)) {
                engine.insert(point);
            }
        }

        done += lines;
    }

    std::size_t rows_total = 0;
    std::printf("Loaded data:\n");
    for (const auto &dataset : DATASETS) {
        const db::MemTable *mem_table = engine.find_mem_table(dataset.name);
        if (mem_table == nullptr) continue;
        rows_total += mem_table->row_count();
        std::printf("  %-8s %10zu rows\n", dataset.name, mem_table->row_count());
    }
    std::printf("  total    %10zu rows, %.1f MB of text\n\n", rows_total, bytes_total / 1e6);

    std::uint64_t first_ts = START_TIMESTAMP;
    std::uint64_t span = generator.last_timestamp() - first_ts;
    std::uint64_t mid_from = first_ts + span * 45 / 100;
    std::uint64_t mid_to = first_ts + span * 55 / 100;

    std::vector<NamedQuery> queries = {
        {"cpu.usage_user, no filter",
            make_query("cpu", "usage_user", db::OperationType::AVG)},
        {"cpu.usage_user, 1 tag",
            make_query("cpu", "usage_user", db::OperationType::MAX, {"host=host_42"})},
        {"sensor.temperature, 2 tags",
            make_query("sensor", "temperature", db::OperationType::AVG, {"env=prod", "region=eu-west"})},
        {"sensor.temperature, 3 tags",
            make_query("sensor", "temperature", db::OperationType::MIN, {"env=prod", "host=host_7", "region=eu-west"})},
        {"disk.read_mb",
            make_query("disk", "read_mb", db::OperationType::AVG, {}, mid_from, mid_to)},
        {"memory.used_percent, 1 tag",
            make_query("memory", "used_percent", db::OperationType::COUNT, {"env=staging"}, mid_from, mid_to)},
    };

    std::printf("CPU queries (%d runs each, best, median and average time):\n", QUERY_REPEATS);
    std::printf("  %-48s %-6s %14s %10s %10s %10s\n",
                "query", "op", "result", "best ms", "median ms", "avg ms");

    std::vector<Timing> cpu_timings;
    std::vector<std::optional<double>> cpu_results;

    for (const auto &named : queries) {
        std::optional<double> result;
        Timing timing = measure([&]() { result = engine.query(named.query); });
        cpu_timings.push_back(timing);
        cpu_results.push_back(result);

        std::printf("  %-48s %-6s %14s %10.3f %10.3f %10.3f\n",
                    named.description, operation_name(named.query.operation_type), format_result(result).c_str(),
                    timing.best * 1e3, timing.median * 1e3, timing.average * 1e3);
    }

#ifdef GPU_DB_HAS_CUDA
    if (!db::GpuExecutor::device_available()) {
        std::printf("\nGPU: no CUDA device available, skipping\n");
        return 0;
    }

    std::map<std::string, std::unique_ptr<db::GpuExecutor>> gpu_executors;
    double transfer_seconds = 0.0;
    for (const auto &dataset : DATASETS) {
        const db::MemTable *mem_table = engine.find_mem_table(dataset.name);
        if (mem_table == nullptr) continue;

        auto executor = std::make_unique<db::GpuExecutor>();
        executor->upload(*mem_table);
        transfer_seconds += executor->transfer_seconds();

        gpu_executors[dataset.name] = std::move(executor);
    }

    std::printf("\nGPU upload, RAM -> VRAM: %.1f ms\n\n", transfer_seconds * 1e3);

    std::printf("GPU queries (%d runs each, best, median and average time):\n", QUERY_REPEATS);
    std::printf("  %-48s %-6s %14s %10s %10s %10s %12s\n",
                "query", "op", "result", "best ms", "median ms", "avg ms", "vs CPU");

    for (std::size_t q = 0; q < queries.size(); ++q) {
        const NamedQuery &named = queries[q];
        const db::MemTable *mem_table = engine.find_mem_table(named.query.dataset);
        db::GpuExecutor &gpu = *gpu_executors.at(named.query.dataset);

        auto run = [&]() -> std::optional<double> {
            db::ResolvedQuery resolved;
            if (db::resolve_query(*mem_table, named.query, resolved) == db::ResolveResult::NO_MATCH) {
                return db::finalize(db::AggState{}, named.query.operation_type);
            }
            return db::finalize(gpu.run_query(resolved), named.query.operation_type);
        };

        run();

        std::optional<double> result;
        Timing timing = measure([&]() { result = run(); });

        char speedup[32];
        std::snprintf(speedup, sizeof(speedup), "%.2fx", cpu_timings[q].median / timing.median);

        std::printf("  %-48s %-6s %14s %10.3f %10.3f %10.3f %12s\n",
                    named.description, operation_name(named.query.operation_type), format_result(result).c_str(),
                    timing.best * 1e3, timing.median * 1e3, timing.average * 1e3, speedup);

        bool same_presence = result.has_value() == cpu_results[q].has_value();
        bool same_value = !result || !cpu_results[q] ||
                          std::abs(*result - *cpu_results[q]) <= 1e-9 * std::max(1.0, std::abs(*cpu_results[q]));
        if (!same_presence || !same_value) {
            std::printf("  warning: GPU result differs from CPU result %s\n", format_result(cpu_results[q]).c_str());
        }
    }
#endif

    return 0;
}
