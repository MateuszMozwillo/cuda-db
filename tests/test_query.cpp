#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "db/parser.hpp"
#include "db/mem_table.hpp"
#include "db/query.hpp"

using namespace db;

static void insert_line(MemTable &mem_table, const char* input) {
    DataPoint point;
    REQUIRE(parse_line(input, point) == true);

    PreparedDp prepared;
    REQUIRE(MemTable::prepare(point, prepared) == true);
    mem_table.commit(point, prepared);
}

// series 1: host=a,location=Krakow,version=1
// series 2: host=a,location=Warsaw,version=1
// series 3: host=b,location=Krakow,version=1
// series 4: host=a,location=Krakow,version=2
// series 5: no tags
// fields:   temperature = 1, pressure = 2
static void fill_mem_table(MemTable &mem_table) {
    insert_line(mem_table, "sensor,host=a,location=Krakow,version=1 temperature=20.0,pressure=1000.0 100\n");
    insert_line(mem_table, "sensor,host=a,location=Warsaw,version=1 temperature=21.0 200\n");
    insert_line(mem_table, "sensor,host=b,location=Krakow,version=1 temperature=22.0 300\n");
    insert_line(mem_table, "sensor,host=a,location=Krakow,version=2 temperature=23.0 400\n");
    insert_line(mem_table, "sensor temperature=24.0 500\n");
}

static Query make_query(const char* field, std::vector<std::string> tags = {}) {
    Query query;
    query.dataset = "sensor";
    query.field = field;
    query.tags = std::move(tags);
    query.operation_type = OperationType::AVG;
    return query;
}

TEST_CASE("resolve_query fields and time range", "[query]") {
    MemTable mem_table;
    fill_mem_table(mem_table);
    ResolvedQuery resolved;

    SECTION("Query without tags has no series filter") {
        Query query = make_query("temperature");

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.field_id == 1);
        REQUIRE(resolved.has_series_filter == false);
        REQUIRE(resolved.series_filter.empty() == true);
        REQUIRE(resolved.operation_type == OperationType::AVG);
    }

    SECTION("Field name is resolved to its id") {
        Query query = make_query("pressure");

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.field_id == 2);
    }

    SECTION("Operation type is copied") {
        Query query = make_query("temperature");
        query.operation_type = OperationType::MAX;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.operation_type == OperationType::MAX);
    }

    SECTION("Unknown field gives no match") {
        Query query = make_query("humidity");

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Default time range covers everything") {
        Query query = make_query("temperature");

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.from == 0);
        REQUIRE(resolved.to == std::numeric_limits<std::uint64_t>::max());
    }

    SECTION("Time range is copied") {
        Query query = make_query("temperature");
        query.from = 150;
        query.to = 450;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.from == 150);
        REQUIRE(resolved.to == 450);
    }

    SECTION("Empty time range gives no match") {
        Query query = make_query("temperature");
        query.from = 300;
        query.to = 300;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Reversed time range gives no match") {
        Query query = make_query("temperature");
        query.from = 400;
        query.to = 100;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Empty mem table gives no match") {
        MemTable empty_mem_table;
        Query query = make_query("temperature");

        REQUIRE(resolve_query(empty_mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }
}

TEST_CASE("resolve_query series filter", "[query]") {
    MemTable mem_table;
    fill_mem_table(mem_table);
    ResolvedQuery resolved;

    SECTION("Single tag") {
        Query query = make_query("temperature", {"location=Krakow"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.has_series_filter == true);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 0, 1, 1, 0});
    }

    SECTION("Filter has one entry per series plus unused index 0") {
        Query query = make_query("temperature", {"host=b"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter.size() == mem_table.series_count() + 1);
        REQUIRE(resolved.series_filter[0] == 0);
    }

    SECTION("Two tags are combined with AND") {
        Query query = make_query("temperature", {"location=Krakow", "version=1"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 0, 1, 0, 0});
    }

    SECTION("Three tags are combined with AND") {
        Query query = make_query("temperature", {"host=a", "location=Krakow", "version=1"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 0, 0, 0, 0});
    }

    SECTION("Order of tags in the query does not matter") {
        Query query = make_query("temperature", {"version=1", "location=Krakow", "host=a"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 0, 0, 0, 0});
    }

    SECTION("Repeated tag in the query") {
        Query query = make_query("temperature", {"location=Krakow", "location=Krakow"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 0, 1, 1, 0});
    }

    SECTION("Unknown tag gives no match") {
        Query query = make_query("temperature", {"location=Gdansk"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Unknown tag among known tags gives no match") {
        Query query = make_query("temperature", {"location=Krakow", "location=Gdansk", "version=1"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Tags that never appear together give no match") {
        Query query = make_query("temperature", {"location=Warsaw", "version=2"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Reused result does not keep the filter from a previous query") {
        Query with_tags = make_query("temperature", {"location=Krakow"});
        REQUIRE(resolve_query(mem_table, with_tags, resolved) == ResolveResult::OK);
        REQUIRE(resolved.has_series_filter == true);

        Query without_tags = make_query("temperature");
        REQUIRE(resolve_query(mem_table, without_tags, resolved) == ResolveResult::OK);
        REQUIRE(resolved.has_series_filter == false);
        REQUIRE(resolved.series_filter.empty() == true);
    }
}

TEST_CASE("resolve_query edge cases", "[query]") {
    MemTable mem_table;
    fill_mem_table(mem_table);
    ResolvedQuery resolved;

    SECTION("Smallest non-empty time range at zero") {
        Query query = make_query("temperature");
        query.from = 0;
        query.to = 1;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
    }

    SECTION("Smallest non-empty time range at the maximum") {
        Query query = make_query("temperature");
        query.from = std::numeric_limits<std::uint64_t>::max() - 1;
        query.to = std::numeric_limits<std::uint64_t>::max();

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
    }

    SECTION("Empty time range at zero") {
        Query query = make_query("temperature");
        query.from = 0;
        query.to = 0;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Empty time range at the maximum") {
        Query query = make_query("temperature");
        query.from = std::numeric_limits<std::uint64_t>::max();
        query.to = std::numeric_limits<std::uint64_t>::max();

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Time range outside of the data is still OK") {
        Query query = make_query("temperature");
        query.from = 1'000'000;
        query.to = 2'000'000;

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
    }

    SECTION("Field names are case sensitive") {
        Query query = make_query("Temperature");

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Field name with trailing space is not the same field") {
        Query query = make_query("temperature ");

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Tag names are case sensitive") {
        Query query = make_query("temperature", {"location=krakow"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Field used as a tag gives no match") {
        Query query = make_query("temperature", {"temperature=20.0"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Dataset name is not checked by resolve_query") {
        Query query = make_query("temperature");
        query.dataset = "cpu";

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
    }

    SECTION("Series filter does not check if the series has the queried field") {
        Query query = make_query("pressure", {"host=b"});

        REQUIRE(resolve_query(mem_table, query, resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 0, 0, 1, 0, 0});
    }

    SECTION("Tag shared by every tagged series") {
        MemTable shared;
        insert_line(shared, "sensor,region=eu,host=a temperature=1.0 1\n");
        insert_line(shared, "sensor,region=eu,host=b temperature=2.0 2\n");
        insert_line(shared, "sensor,region=eu,host=c temperature=3.0 3\n");

        REQUIRE(resolve_query(shared, make_query("temperature", {"region=eu"}), resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 1, 1});
    }

    SECTION("Filter marks the highest series id") {
        MemTable single;
        insert_line(single, "sensor,host=a temperature=1.0 1\n");

        REQUIRE(resolve_query(single, make_query("temperature", {"host=a"}), resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1});
    }

    SECTION("Series without tags can not be matched by a tag filter") {
        MemTable untagged;
        insert_line(untagged, "sensor temperature=1.0 1\n");

        REQUIRE(resolve_query(untagged, make_query("temperature"), resolved) == ResolveResult::OK);
        REQUIRE(resolve_query(untagged, make_query("temperature", {"host=a"}), resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Escaped tag is matched by its raw form") {
        MemTable escaped;
        insert_line(escaped, "sensor,location=New\\ York temperature=1.0 1\n");

        REQUIRE(resolve_query(escaped, make_query("temperature", {"location=New\\ York"}), resolved) == ResolveResult::OK);
        REQUIRE(resolve_query(escaped, make_query("temperature", {"location=New York"}), resolved) == ResolveResult::NO_MATCH);
    }

    SECTION("Maximum number of tags") {
        MemTable many;
        std::string line = "sensor";
        std::vector<std::string> tags;
        for (unsigned int i = 0; i < MAX_TAG_COUNT; ++i) {
            std::string tag = "tag" + std::to_string(i) + "=v";
            line += "," + tag;
            tags.push_back(tag);
        }
        line += " temperature=1.0 1\n";
        insert_line(many, line.c_str());
        insert_line(many, "sensor,tag0=v temperature=2.0 2\n");

        REQUIRE(resolve_query(many, make_query("temperature", tags), resolved) == ResolveResult::OK);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 1, 0});
    }

    SECTION("Result is usable after a previous NO_MATCH") {
        REQUIRE(resolve_query(mem_table, make_query("temperature", {"location=Gdansk"}), resolved) == ResolveResult::NO_MATCH);

        REQUIRE(resolve_query(mem_table, make_query("pressure", {"host=b"}), resolved) == ResolveResult::OK);
        REQUIRE(resolved.field_id == 2);
        REQUIRE(resolved.has_series_filter == true);
        REQUIRE(resolved.series_filter == std::vector<std::uint8_t>{0, 0, 0, 1, 0, 0});
    }
}

TEST_CASE("resolve_query leaves result consistent", "[query]") {
    MemTable mem_table;
    fill_mem_table(mem_table);
    ResolvedQuery resolved;

    auto filter_is_consistent = [&]() {
        return !resolved.has_series_filter
            || resolved.series_filter.size() == mem_table.series_count() + 1;
    };

    SECTION("NO_MATCH on unknown tag after a query without tags") {
        REQUIRE(resolve_query(mem_table, make_query("temperature"), resolved) == ResolveResult::OK);

        REQUIRE(resolve_query(mem_table, make_query("temperature", {"location=Gdansk"}), resolved) == ResolveResult::NO_MATCH);
        REQUIRE(filter_is_consistent());
    }

    SECTION("NO_MATCH on disjoint tags after a query without tags") {
        REQUIRE(resolve_query(mem_table, make_query("temperature"), resolved) == ResolveResult::OK);

        REQUIRE(resolve_query(mem_table, make_query("temperature", {"location=Warsaw", "version=2"}), resolved) == ResolveResult::NO_MATCH);
        REQUIRE(filter_is_consistent());
    }
}
