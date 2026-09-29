#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "db/parser.hpp"
#include "db/mem_table.hpp"
#include "db/query.hpp"
#include "db/cpu_executor.hpp"

using namespace db;

static constexpr double INF = std::numeric_limits<double>::infinity();
static constexpr std::uint64_t MAX_TS = std::numeric_limits<std::uint64_t>::max();

static void insert_line(MemTable &mem_table, const char* input) {
    DataPoint point;
    REQUIRE(parse_line(input, point) == true);

    PreparedDp prepared;
    REQUIRE(MemTable::prepare(point, prepared) == true);
    mem_table.commit(point, prepared);
}

// series 1: host=a,loc=Krakow   temperature 10 @100, 20 @200, 30 @300, pressure 1000 @100
// series 2: host=b,loc=Krakow   temperature -5 @150, 15 @250
// series 3: host=a,loc=Warsaw   temperature 100 @100
// series 4: no tags             temperature 7 @400
static void fill_mem_table(MemTable &mem_table) {
    insert_line(mem_table, "sensor,host=a,loc=Krakow temperature=10,pressure=1000 100\n");
    insert_line(mem_table, "sensor,host=b,loc=Krakow temperature=-5 150\n");
    insert_line(mem_table, "sensor,host=a,loc=Krakow temperature=20 200\n");
    insert_line(mem_table, "sensor,host=b,loc=Krakow temperature=15 250\n");
    insert_line(mem_table, "sensor,host=a,loc=Krakow temperature=30 300\n");
    insert_line(mem_table, "sensor,host=a,loc=Warsaw temperature=100 100\n");
    insert_line(mem_table, "sensor temperature=7 400\n");
}

static AggState run_query(const MemTable &mem_table, const char* field,
                          std::vector<std::string> tags = {},
                          std::uint64_t from = 0, std::uint64_t to = MAX_TS) {
    Query query;
    query.dataset = "sensor";
    query.field = field;
    query.tags = std::move(tags);
    query.from = from;
    query.to = to;
    query.operation_type = OperationType::AVG;

    ResolvedQuery resolved;
    if (resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH) {
        return AggState{};
    }
    return handle_cpu_query(mem_table, resolved);
}

static void require_state(const AggState &state, std::uint64_t count, double sum, double min, double max) {
    REQUIRE(state.count == count);
    REQUIRE(state.sum == sum);
    REQUIRE(state.min == min);
    REQUIRE(state.max == max);
}

static void require_empty(const AggState &state) {
    require_state(state, 0, 0.0, INF, -INF);
}

TEST_CASE("handle_cpu_query field filter", "[cpu_executor]") {
    MemTable mem_table;
    fill_mem_table(mem_table);

    SECTION("All rows of a field") {
        require_state(run_query(mem_table, "temperature"), 7, 177.0, -5.0, 100.0);
    }

    SECTION("Other field from the same line is not counted") {
        require_state(run_query(mem_table, "pressure"), 1, 1000.0, 1000.0, 1000.0);
    }

    SECTION("Field id that does not exist gives empty state") {
        ResolvedQuery resolved;
        resolved.field_id = 99;

        require_empty(handle_cpu_query(mem_table, resolved));
    }

    SECTION("Field id 0 is never used by rows") {
        ResolvedQuery resolved;
        resolved.field_id = 0;

        require_empty(handle_cpu_query(mem_table, resolved));
    }

    SECTION("Empty mem table gives empty state") {
        MemTable empty;
        ResolvedQuery resolved;
        resolved.field_id = 1;

        require_empty(handle_cpu_query(empty, resolved));
    }
}

TEST_CASE("handle_cpu_query time range", "[cpu_executor]") {
    MemTable mem_table;
    fill_mem_table(mem_table);

    SECTION("From is inclusive") {
        require_state(run_query(mem_table, "temperature", {}, 100, 150), 2, 110.0, 10.0, 100.0);
    }

    SECTION("To is exclusive") {
        require_state(run_query(mem_table, "temperature", {}, 200, 250), 1, 20.0, 20.0, 20.0);
    }

    SECTION("Range covering several timestamps") {
        require_state(run_query(mem_table, "temperature", {}, 100, 200), 3, 105.0, -5.0, 100.0);
    }

    SECTION("Range of exactly one nanosecond") {
        require_state(run_query(mem_table, "temperature", {}, 300, 301), 1, 30.0, 30.0, 30.0);
    }

    SECTION("Range ending right before a point") {
        require_empty(run_query(mem_table, "temperature", {}, 301, 400));
    }

    SECTION("Range before all data") {
        require_empty(run_query(mem_table, "temperature", {}, 0, 100));
    }

    SECTION("Range after all data") {
        require_empty(run_query(mem_table, "temperature", {}, 401, MAX_TS));
    }

    SECTION("Range between points") {
        require_empty(run_query(mem_table, "temperature", {}, 101, 150));
    }

    SECTION("Open ended range includes the last point") {
        require_state(run_query(mem_table, "temperature", {}, 400, MAX_TS), 1, 7.0, 7.0, 7.0);
    }

    SECTION("Point with timestamp 0 is found by the default range") {
        MemTable zero;
        insert_line(zero, "sensor v=1 0\n");

        require_state(run_query(zero, "v"), 1, 1.0, 1.0, 1.0);
    }

    SECTION("Point with the largest allowed timestamp is found by the default range") {
        MemTable last;
        insert_line(last, "sensor v=1 18446744073709551614\n");

        require_state(run_query(last, "v"), 1, 1.0, 1.0, 1.0);
    }

    SECTION("Manually built empty range gives empty state") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.from = 200;
        resolved.to = 200;

        require_empty(handle_cpu_query(mem_table, resolved));
    }

    SECTION("Manually built reversed range gives empty state") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.from = 300;
        resolved.to = 100;

        require_empty(handle_cpu_query(mem_table, resolved));
    }
}

TEST_CASE("handle_cpu_query series filter", "[cpu_executor]") {
    MemTable mem_table;
    fill_mem_table(mem_table);

    SECTION("Single tag") {
        require_state(run_query(mem_table, "temperature", {"host=a"}), 4, 160.0, 10.0, 100.0);
    }

    SECTION("Single tag shared by several series") {
        require_state(run_query(mem_table, "temperature", {"loc=Krakow"}), 5, 70.0, -5.0, 30.0);
    }

    SECTION("Two tags") {
        require_state(run_query(mem_table, "temperature", {"loc=Krakow", "host=b"}), 2, 10.0, -5.0, 15.0);
    }

    SECTION("Tags and time range together") {
        require_state(run_query(mem_table, "temperature", {"host=a"}, 150, 400), 2, 50.0, 20.0, 30.0);
    }

    SECTION("Tag filter and field that the series does not have") {
        require_empty(run_query(mem_table, "pressure", {"host=b"}));
    }

    SECTION("Series without tags is excluded by any tag filter") {
        AggState state = run_query(mem_table, "temperature", {"host=a"});

        REQUIRE(state.max != 7.0);
        REQUIRE(state.count == 4);
    }

    SECTION("Filter flag off ignores the bitmap") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.has_series_filter = false;
        resolved.series_filter = {0, 0, 0, 0, 0};

        require_state(handle_cpu_query(mem_table, resolved), 7, 177.0, -5.0, 100.0);
    }

    SECTION("Bitmap with all zeros gives empty state") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.has_series_filter = true;
        resolved.series_filter = {0, 0, 0, 0, 0};

        require_empty(handle_cpu_query(mem_table, resolved));
    }

    SECTION("Bitmap with all ones gives all rows") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.has_series_filter = true;
        resolved.series_filter = {0, 1, 1, 1, 1};

        require_state(handle_cpu_query(mem_table, resolved), 7, 177.0, -5.0, 100.0);
    }

    SECTION("Bitmap selecting only the last series") {
        ResolvedQuery resolved;
        resolved.field_id = 1;
        resolved.has_series_filter = true;
        resolved.series_filter = {0, 0, 0, 0, 1};

        require_state(handle_cpu_query(mem_table, resolved), 1, 7.0, 7.0, 7.0);
    }
}

TEST_CASE("handle_cpu_query data edge cases", "[cpu_executor]") {
    SECTION("Duplicate points are all counted") {
        MemTable mem_table;
        insert_line(mem_table, "sensor,host=a v=5 100\n");
        insert_line(mem_table, "sensor,host=a v=5 100\n");
        insert_line(mem_table, "sensor,host=a v=5 100\n");

        require_state(run_query(mem_table, "v"), 3, 15.0, 5.0, 5.0);
    }

    SECTION("Points inserted out of time order") {
        MemTable mem_table;
        insert_line(mem_table, "sensor v=3 300\n");
        insert_line(mem_table, "sensor v=1 100\n");
        insert_line(mem_table, "sensor v=2 200\n");

        require_state(run_query(mem_table, "v", {}, 100, 300), 2, 3.0, 1.0, 2.0);
    }

    SECTION("Only negative values") {
        MemTable mem_table;
        insert_line(mem_table, "sensor v=-1 1\n");
        insert_line(mem_table, "sensor v=-2 2\n");

        require_state(run_query(mem_table, "v"), 2, -3.0, -2.0, -1.0);
    }

    SECTION("Series with tags in different order are separate series but both match") {
        MemTable mem_table;
        insert_line(mem_table, "sensor,a=1,b=2 v=1 1\n");
        insert_line(mem_table, "sensor,b=2,a=1 v=2 2\n");

        require_state(run_query(mem_table, "v", {"a=1", "b=2"}), 2, 3.0, 1.0, 2.0);
    }

    SECTION("Rows added to an existing series after resolve are counted") {
        MemTable mem_table;
        insert_line(mem_table, "sensor,host=a v=1 1\n");

        Query query;
        query.field = "v";
        query.tags = {"host=a"};
        query.operation_type = OperationType::COUNT;
        ResolvedQuery resolved;
        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);

        insert_line(mem_table, "sensor,host=a v=2 2\n");

        require_state(handle_cpu_query(mem_table, resolved), 2, 3.0, 1.0, 2.0);
    }
}

TEST_CASE("handle_cpu_query against a reference implementation", "[cpu_executor]") {
    MemTable mem_table;

    struct Row {
        int host;
        std::uint64_t ts;
        double value;
    };
    std::vector<Row> rows;

    for (int i = 0; i < 20000; ++i) {
        Row row{i % 7, static_cast<std::uint64_t>(i) * 10, static_cast<double>((i * 37) % 201 - 100)};
        rows.push_back(row);

        std::string line = "sensor,host=h" + std::to_string(row.host) +
                           " v=" + std::to_string(static_cast<int>(row.value)) +
                           " " + std::to_string(row.ts) + "\n";
        insert_line(mem_table, line.c_str());
    }

    auto reference = [&](int host, std::uint64_t from, std::uint64_t to) {
        AggState expected;
        for (const Row &row : rows) {
            if (host >= 0 && row.host != host) continue;
            if (row.ts < from || row.ts >= to) continue;
            expected.update(row.value);
        }
        return expected;
    };

    SECTION("Whole data set") {
        AggState expected = reference(-1, 0, MAX_TS);
        AggState actual = run_query(mem_table, "v");

        require_state(actual, expected.count, expected.sum, expected.min, expected.max);
        REQUIRE(actual.count == 20000);
    }

    SECTION("Tag filter") {
        AggState expected = reference(3, 0, MAX_TS);

        require_state(run_query(mem_table, "v", {"host=h3"}), expected.count, expected.sum, expected.min, expected.max);
    }

    SECTION("Tag filter and time range") {
        AggState expected = reference(5, 12345, 150001);

        require_state(run_query(mem_table, "v", {"host=h5"}, 12345, 150001),
                      expected.count, expected.sum, expected.min, expected.max);
    }

    SECTION("Splitting the time range and merging gives the same result") {
        AggState full = run_query(mem_table, "v", {"host=h2"}, 1000, 190000);
        AggState left = run_query(mem_table, "v", {"host=h2"}, 1000, 77777);
        AggState right = run_query(mem_table, "v", {"host=h2"}, 77777, 190000);

        left.merge(right);

        require_state(left, full.count, full.sum, full.min, full.max);
    }

    SECTION("Every host together covers the whole data set") {
        AggState merged;
        for (int host = 0; host < 7; ++host) {
            merged.merge(run_query(mem_table, "v", {"host=h" + std::to_string(host)}));
        }

        AggState full = run_query(mem_table, "v");
        require_state(merged, full.count, full.sum, full.min, full.max);
    }
}

TEST_CASE("handle_cpu_query with multiple threads", "[cpu_executor]") {
    MemTable mem_table;
    for (int i = 0; i < 20000; ++i) {
        std::string line = "sensor,host=h" + std::to_string(i % 7) +
                           " v=" + std::to_string((i * 37) % 201 - 100) +
                           " " + std::to_string(static_cast<std::uint64_t>(i) * 10) + "\n";
        insert_line(mem_table, line.c_str());
    }

    auto resolve = [&](std::vector<std::string> tags, std::uint64_t from, std::uint64_t to) {
        Query query;
        query.field = "v";
        query.tags = std::move(tags);
        query.from = from;
        query.to = to;
        query.operation_type = OperationType::AVG;

        ResolvedQuery resolved;
        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        return resolved;
    };

    auto require_same_for_all_thread_counts = [&](const ResolvedQuery &resolved) {
        AggState single = handle_cpu_query(mem_table, resolved, 1);
        for (unsigned threads : {2u, 3u, 4u, 7u, 8u, 16u, 64u, 1000u}) {
            AggState multi = handle_cpu_query(mem_table, resolved, threads);
            require_state(multi, single.count, single.sum, single.min, single.max);
        }
        AggState automatic = handle_cpu_query(mem_table, resolved);
        require_state(automatic, single.count, single.sum, single.min, single.max);
    };

    SECTION("No filters") {
        ResolvedQuery resolved = resolve({}, 0, MAX_TS);

        require_same_for_all_thread_counts(resolved);
        REQUIRE(handle_cpu_query(mem_table, resolved, 8).count == 20000);
    }

    SECTION("Tag filter") {
        require_same_for_all_thread_counts(resolve({"host=h3"}, 0, MAX_TS));
    }

    SECTION("Time range that falls into a single thread's chunk") {
        require_same_for_all_thread_counts(resolve({}, 100, 200));
    }

    SECTION("Time range that crosses chunk boundaries") {
        require_same_for_all_thread_counts(resolve({"host=h5"}, 12345, 150001));
    }

    SECTION("Only the first and the last row match") {
        MemTable edges;
        insert_line(edges, "sensor,host=x v=1 1\n");
        for (int i = 0; i < 1000; ++i) {
            std::string line = "sensor,host=y v=" + std::to_string(i) + " " + std::to_string(i + 2) + "\n";
            insert_line(edges, line.c_str());
        }
        insert_line(edges, "sensor,host=x v=2 5000\n");

        Query query;
        query.field = "v";
        query.tags = {"host=x"};
        query.operation_type = OperationType::AVG;
        ResolvedQuery resolved;
        REQUIRE(resolve_query(edges, query, resolved) == ResolveResult::OK);

        for (unsigned threads : {1u, 2u, 3u, 16u, 1002u}) {
            require_state(handle_cpu_query(edges, resolved, threads), 2, 3.0, 1.0, 2.0);
        }
    }
}

TEST_CASE("handle_cpu_query thread count limits", "[cpu_executor]") {
    SECTION("More threads than rows") {
        MemTable mem_table;
        fill_mem_table(mem_table);
        ResolvedQuery resolved;
        resolved.field_id = 1;

        require_state(handle_cpu_query(mem_table, resolved, 64), 7, 177.0, -5.0, 100.0);
    }

    SECTION("Single row with many threads") {
        MemTable mem_table;
        insert_line(mem_table, "sensor v=3 1\n");
        ResolvedQuery resolved;
        resolved.field_id = 1;

        require_state(handle_cpu_query(mem_table, resolved, 16), 1, 3.0, 3.0, 3.0);
    }

    SECTION("Empty mem table with many threads") {
        MemTable mem_table;
        ResolvedQuery resolved;
        resolved.field_id = 1;

        require_empty(handle_cpu_query(mem_table, resolved, 16));
    }
}
