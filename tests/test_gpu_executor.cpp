#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include "db/parser.hpp"
#include "db/mem_table.hpp"
#include "db/query.hpp"
#include "db/cpu_executor.hpp"
#include "db/gpu_executor.hpp"

using namespace db;

static constexpr std::uint64_t MAX_TS = std::numeric_limits<std::uint64_t>::max();
static constexpr double INF = std::numeric_limits<double>::infinity();

#define REQUIRE_GPU() \
    if (!GpuExecutor::device_available()) SKIP("no CUDA device available")

static void insert_line(MemTable &mem_table, const std::string &input) {
    DataPoint point;
    REQUIRE(parse_line(input.c_str(), point) == true);

    PreparedDp prepared;
    REQUIRE(MemTable::prepare(point, prepared) == true);
    mem_table.commit(point, prepared);
}

static ResolvedQuery resolve(const MemTable &mem_table, const char* field,
                             std::vector<std::string> tags = {},
                             std::uint64_t from = 0, std::uint64_t to = MAX_TS) {
    Query query;
    query.field = field;
    query.tags = std::move(tags);
    query.from = from;
    query.to = to;
    query.operation_type = OperationType::AVG;

    ResolvedQuery resolved;
    REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
    return resolved;
}

// the GPU adds values in a different order, so only the sum may differ (in the last bits)
static void require_same(const AggState &gpu, const AggState &cpu) {
    REQUIRE(gpu.count == cpu.count);
    REQUIRE(gpu.min == cpu.min);
    REQUIRE(gpu.max == cpu.max);
    REQUIRE(gpu.sum == Catch::Approx(cpu.sum).epsilon(1e-12).margin(1e-9));
}

static void require_gpu_matches_cpu(GpuExecutor &gpu, const MemTable &mem_table, const ResolvedQuery &resolved) {
    AggState cpu_state = handle_cpu_query(mem_table, resolved, 1);
    AggState gpu_state = gpu.run_query(resolved);
    require_same(gpu_state, cpu_state);
}

// series 1: host=a,loc=Krakow   temperature 10 @100, 20 @200, 30 @300, pressure 1000 @100
// series 2: host=b,loc=Krakow   temperature -5 @150, 15 @250
// series 3: host=a,loc=Warsaw   temperature 100 @100
// series 4: no tags             temperature 7 @400
static void fill_small(MemTable &mem_table) {
    insert_line(mem_table, "sensor,host=a,loc=Krakow temperature=10,pressure=1000 100\n");
    insert_line(mem_table, "sensor,host=b,loc=Krakow temperature=-5 150\n");
    insert_line(mem_table, "sensor,host=a,loc=Krakow temperature=20 200\n");
    insert_line(mem_table, "sensor,host=b,loc=Krakow temperature=15 250\n");
    insert_line(mem_table, "sensor,host=a,loc=Krakow temperature=30 300\n");
    insert_line(mem_table, "sensor,host=a,loc=Warsaw temperature=100 100\n");
    insert_line(mem_table, "sensor temperature=7 400\n");
}

// rows with values -100..100 (integers, so sums are exact), 7 hosts, 2 fields, increasing timestamps
static void fill_large(MemTable &mem_table, int lines) {
    for (int i = 0; i < lines; ++i) {
        insert_line(mem_table, "sensor,host=h" + std::to_string(i % 7) +
                               " v=" + std::to_string((i * 37) % 201 - 100) +
                               ",w=" + std::to_string((i * 13) % 101) +
                               " " + std::to_string(static_cast<std::uint64_t>(i) * 10) + "\n");
    }
}

TEST_CASE("GpuExecutor on a small mem table", "[gpu]") {
    REQUIRE_GPU();

    MemTable mem_table;
    fill_small(mem_table);
    GpuExecutor gpu;
    gpu.upload(mem_table);

    REQUIRE(gpu.uploaded_rows() == mem_table.row_count());

    SECTION("Field without other filters") {
        AggState state = gpu.run_query(resolve(mem_table, "temperature"));

        REQUIRE(state.count == 7);
        REQUIRE(state.sum == 177.0);
        REQUIRE(state.min == -5.0);
        REQUIRE(state.max == 100.0);
    }

    SECTION("Other field") {
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "pressure"));
    }

    SECTION("Ranges starting and ending exactly at points") {
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {}, 100, 150));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {}, 200, 250));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {}, 300, 301));
    }

    SECTION("Time range without data") {
        AggState state = gpu.run_query(resolve(mem_table, "temperature", {}, 101, 150));

        REQUIRE(state.count == 0);
        REQUIRE(state.min == INF);
        REQUIRE(state.max == -INF);
    }

    SECTION("Tag filters") {
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {"host=a"}));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {"loc=Krakow", "host=b"}));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {"host=a"}, 150, 400));
    }

    SECTION("Tag of a series without the queried field") {
        AggState state = gpu.run_query(resolve(mem_table, "pressure", {"host=b"}));

        REQUIRE(state.count == 0);
    }

    SECTION("Filter flag off with a non-empty bitmap") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.has_series_filter = false;
        resolved.series_filter = {0, 0, 0, 0, 0};

        require_gpu_matches_cpu(gpu, mem_table, resolved);
        REQUIRE(gpu.run_query(resolved).count == 7);
    }

    SECTION("Bitmap with all zeros") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.has_series_filter = true;
        resolved.series_filter = {0, 0, 0, 0, 0};

        REQUIRE(gpu.run_query(resolved).count == 0);
    }

    SECTION("Field id that does not exist") {
        ResolvedQuery resolved;
        resolved.field_id = 99;

        REQUIRE(gpu.run_query(resolved).count == 0);
    }

    SECTION("60 queries on one executor") {
        for (int i = 0; i < 20; ++i) {
            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {"host=a"}));
            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature"));
            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "pressure", {}, 0, 101));
        }
    }
}

TEST_CASE("GpuExecutor edge cases", "[gpu]") {
    REQUIRE_GPU();

    SECTION("Query before any upload") {
        GpuExecutor gpu;
        ResolvedQuery resolved;
        resolved.field_id = 1;

        AggState state = gpu.run_query(resolved);
        REQUIRE(state.count == 0);
        REQUIRE(state.sum == 0.0);
    }

    SECTION("Empty mem table") {
        MemTable mem_table;
        GpuExecutor gpu;
        gpu.upload(mem_table);
        ResolvedQuery resolved;
        resolved.field_id = 1;

        REQUIRE(gpu.uploaded_rows() == 0);
        REQUIRE(gpu.run_query(resolved).count == 0);
    }

    SECTION("Single row") {
        MemTable mem_table;
        insert_line(mem_table, "sensor v=-2.5 1\n");
        GpuExecutor gpu;
        gpu.upload(mem_table);

        AggState state = gpu.run_query(resolve(mem_table, "v"));
        REQUIRE(state.count == 1);
        REQUIRE(state.sum == -2.5);
        REQUIRE(state.min == -2.5);
        REQUIRE(state.max == -2.5);
    }

    SECTION("Only negative values") {
        MemTable mem_table;
        insert_line(mem_table, "sensor v=-10 1\n");
        insert_line(mem_table, "sensor v=-3 2\n");
        insert_line(mem_table, "sensor v=-7 3\n");
        GpuExecutor gpu;
        gpu.upload(mem_table);

        AggState state = gpu.run_query(resolve(mem_table, "v"));
        REQUIRE(state.min == -10.0);
        REQUIRE(state.max == -3.0);
    }

    SECTION("Rows inserted after upload") {
        MemTable mem_table;
        insert_line(mem_table, "sensor v=1 1\n");
        GpuExecutor gpu;
        gpu.upload(mem_table);

        insert_line(mem_table, "sensor v=2 2\n");
        ResolvedQuery resolved = resolve(mem_table, "v");

        REQUIRE(gpu.run_query(resolved).count == 1);

        gpu.upload(mem_table);
        REQUIRE(gpu.run_query(resolved).count == 2);
        require_gpu_matches_cpu(gpu, mem_table, resolved);
    }

    SECTION("Smaller mem table uploaded after a bigger one") {
        MemTable big;
        fill_large(big, 5000);
        MemTable small;
        insert_line(small, "sensor v=42 1\n");

        GpuExecutor gpu;
        gpu.upload(big);
        gpu.upload(small);

        REQUIRE(gpu.uploaded_rows() == 1);
        AggState state = gpu.run_query(resolve(small, "v"));
        REQUIRE(state.count == 1);
        REQUIRE(state.sum == 42.0);
    }

    SECTION("Mem table with 2000 series after one with a single series") {
        MemTable small;
        insert_line(small, "sensor,host=a v=1 1\n");
        GpuExecutor gpu;
        gpu.upload(small);
        REQUIRE(gpu.run_query(resolve(small, "v", {"host=a"})).count == 1);

        MemTable big;
        for (int i = 0; i < 2000; ++i) {
            insert_line(big, "sensor,host=h" + std::to_string(i) + " v=" + std::to_string(i) + " 1\n");
        }
        gpu.upload(big);

        require_gpu_matches_cpu(gpu, big, resolve(big, "v", {"host=h1999"}));
        REQUIRE(gpu.run_query(resolve(big, "v", {"host=h1999"})).sum == 1999.0);
    }

    SECTION("Query with filter, without filter, with filter again") {
        MemTable mem_table;
        fill_small(mem_table);
        GpuExecutor gpu;
        gpu.upload(mem_table);

        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {"host=b"}));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature"));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "temperature", {"host=a"}));
    }

    SECTION("Non integer values") {
        MemTable mem_table;
        for (int i = 0; i < 10000; ++i) {
            insert_line(mem_table, "sensor v=0.1 " + std::to_string(i) + "\n");
        }
        GpuExecutor gpu;
        gpu.upload(mem_table);

        AggState state = gpu.run_query(resolve(mem_table, "v"));
        REQUIRE(state.count == 10000);
        REQUIRE(state.sum == Catch::Approx(1000.0));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v"));
    }
}

TEST_CASE("GpuExecutor row counts around block boundaries", "[gpu]") {
    REQUIRE_GPU();

    // counts around the warp size (32) and the block size (256)
    for (int lines : {1, 2, 31, 32, 33, 127, 128, 129, 255, 256, 257, 511, 512, 513, 1000, 4099}) {
        DYNAMIC_SECTION("lines: " << lines) {
            MemTable mem_table;
            fill_large(mem_table, lines);
            GpuExecutor gpu;
            gpu.upload(mem_table);

            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v"));
            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "w"));
            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {"host=h0"}));
            REQUIRE(gpu.run_query(resolve(mem_table, "v")).count == static_cast<std::uint64_t>(lines));
        }
    }
}

TEST_CASE("GpuExecutor on a large data set", "[gpu]") {
    REQUIRE_GPU();

    // 600 000 rows, more than threads in the grid
    MemTable mem_table;
    fill_large(mem_table, 300000);
    GpuExecutor gpu;
    gpu.upload(mem_table);

    SECTION("No filters") {
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v"));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "w"));
    }

    SECTION("Tag filter") {
        for (int host = 0; host < 7; ++host) {
            require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {"host=h" + std::to_string(host)}));
        }
    }

    SECTION("Time ranges") {
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {}, 0, 1));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {}, 12345, 1234567));
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {}, 2999990, MAX_TS));
    }

    SECTION("Tag filter and time range") {
        require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "w", {"host=h5"}, 777, 2222222));
    }

    SECTION("Time ranges matching only the first or the last row") {
        AggState first = gpu.run_query(resolve(mem_table, "v", {}, 0, 1));
        AggState last = gpu.run_query(resolve(mem_table, "v", {}, 2999990, MAX_TS));

        REQUIRE(first.count == 1);
        REQUIRE(last.count == 1);
    }

    SECTION("Tag filter compared with the multi-threaded CPU executor") {
        ResolvedQuery resolved = resolve(mem_table, "v", {"host=h3"});

        require_same(gpu.run_query(resolved), handle_cpu_query(mem_table, resolved, 8));
    }
}

TEST_CASE("GpuExecutor on random data", "[gpu]") {
    REQUIRE_GPU();

    std::mt19937_64 rng(1234);
    std::uniform_real_distribution<double> value_dist(-1000.0, 1000.0);
    std::uniform_int_distribution<int> host_dist(0, 49);
    std::uniform_int_distribution<std::uint64_t> step_dist(1, 1000);

    MemTable mem_table;
    std::uint64_t ts = 0;
    char value[32];
    for (int i = 0; i < 100000; ++i) {
        ts += step_dist(rng);
        std::snprintf(value, sizeof(value), "%.3f", value_dist(rng));
        insert_line(mem_table, "sensor,host=h" + std::to_string(host_dist(rng)) + " v=" + value + " " + std::to_string(ts) + "\n");
    }

    GpuExecutor gpu;
    gpu.upload(mem_table);

    require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v"));
    require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {"host=h17"}));
    require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {}, ts / 4, ts / 2));
    require_gpu_matches_cpu(gpu, mem_table, resolve(mem_table, "v", {"host=h42"}, ts / 3, ts));
}
