#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <optional>
#include <string>
#include "db/parser.hpp"
#include "db/engine.hpp"

using namespace db;

static bool insert_line(Engine &engine, const char* input) {
    DataPoint point;
    REQUIRE(parse_line(input, point) == true);
    return engine.insert(point);
}

TEST_CASE("Engine insert", "[engine]") {
    Engine engine;

    SECTION("Lines from the same dataset") {
        REQUIRE(insert_line(engine, "sensor,location=Krakow temperature=80.5 1\n") == true);
        REQUIRE(insert_line(engine, "sensor,location=Warsaw temperature=81.0 2\n") == true);

        REQUIRE(engine.dataset_count() == 1);

        const MemTable *sensor = engine.find_mem_table("sensor");
        REQUIRE(sensor != nullptr);
        REQUIRE(sensor->row_count() == 2);
    }

    SECTION("Lines from different datasets") {
        REQUIRE(insert_line(engine, "sensor,location=Krakow temperature=80.5 1\n") == true);
        REQUIRE(insert_line(engine, "cpu,host=server01 usage=42.5 2\n") == true);
        REQUIRE(insert_line(engine, "sensor,location=Krakow temperature=81.0 3\n") == true);

        REQUIRE(engine.dataset_count() == 2);

        const MemTable *sensor = engine.find_mem_table("sensor");
        const MemTable *cpu = engine.find_mem_table("cpu");
        REQUIRE(sensor != nullptr);
        REQUIRE(cpu != nullptr);

        REQUIRE(sensor->row_count() == 2);
        REQUIRE(sensor->field_values()[0] == 80.5);
        REQUIRE(sensor->field_values()[1] == 81.0);

        REQUIRE(cpu->row_count() == 1);
        REQUIRE(cpu->field_values()[0] == 42.5);
    }

    SECTION("Ids of the second dataset") {
        REQUIRE(insert_line(engine, "sensor,location=Krakow temperature=80.5,pressure=1024.1 1\n") == true);
        REQUIRE(insert_line(engine, "cpu,host=server01 usage=42.5 2\n") == true);

        const MemTable *cpu = engine.find_mem_table("cpu");
        REQUIRE(cpu != nullptr);
        REQUIRE(cpu->series_ids()[0] == 1);
        REQUIRE(cpu->field_ids()[0] == 1);
    }

    SECTION("Dataset that was never inserted") {
        REQUIRE(insert_line(engine, "sensor temperature=80.5 1\n") == true);

        REQUIRE(engine.find_mem_table("cpu") == nullptr);
    }

    SECTION("Rejected line with a new dataset") {
        REQUIRE(insert_line(engine, "sensor temperature=abc 1\n") == false);

        REQUIRE(engine.dataset_count() == 0);
        REQUIRE(engine.find_mem_table("sensor") == nullptr);
    }

    SECTION("1000 datasets") {
        REQUIRE(insert_line(engine, "dataset0,location=Krakow temperature=80.5 1\n") == true);

        for (unsigned int i = 1; i < 1000; ++i) {
            std::string input = "dataset" + std::to_string(i) + " value=" + std::to_string(i) + " 1\n";
            REQUIRE(insert_line(engine, input.c_str()) == true);
        }

        REQUIRE(engine.dataset_count() == 1000);

        const MemTable *first = engine.find_mem_table("dataset0");
        REQUIRE(first != nullptr);
        REQUIRE(first->row_count() == 1);
        REQUIRE(first->field_values()[0] == 80.5);

        const MemTable *last = engine.find_mem_table("dataset999");
        REQUIRE(last != nullptr);
        REQUIRE(last->field_values()[0] == 999.0);
    }
}

static Query make_query(const char* dataset, const char* field, OperationType operation_type,
                        std::vector<std::string> tags = {}) {
    Query query;
    query.dataset = dataset;
    query.field = field;
    query.tags = std::move(tags);
    query.operation_type = operation_type;
    return query;
}

// sensor: temperature 10, -5, 20, 15, 30 (loc=Krakow), 100 (loc=Warsaw), 7 (no tags)
// cpu:    temperature 1000
static void fill_engine(Engine &engine) {
    REQUIRE(insert_line(engine, "sensor,host=a,loc=Krakow temperature=10,pressure=1000 100\n") == true);
    REQUIRE(insert_line(engine, "sensor,host=b,loc=Krakow temperature=-5 150\n") == true);
    REQUIRE(insert_line(engine, "sensor,host=a,loc=Krakow temperature=20 200\n") == true);
    REQUIRE(insert_line(engine, "sensor,host=b,loc=Krakow temperature=15 250\n") == true);
    REQUIRE(insert_line(engine, "sensor,host=a,loc=Krakow temperature=30 300\n") == true);
    REQUIRE(insert_line(engine, "sensor,host=a,loc=Warsaw temperature=100 100\n") == true);
    REQUIRE(insert_line(engine, "sensor temperature=7 400\n") == true);
    REQUIRE(insert_line(engine, "cpu,host=a temperature=1000 100\n") == true);
}

TEST_CASE("Engine query", "[engine]") {
    Engine engine;
    fill_engine(engine);

    SECTION("All operations, no filters") {
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::COUNT)) == 7.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MIN)) == -5.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MAX)) == 100.0);

        std::optional<double> avg = engine.query(make_query("sensor", "temperature", OperationType::AVG));
        REQUIRE(avg.has_value());
        REQUIRE(*avg == Catch::Approx(177.0 / 7.0));
    }

    SECTION("Tag filter") {
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::COUNT, {"loc=Krakow"})) == 5.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::AVG, {"loc=Krakow"})) == 14.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MAX, {"loc=Krakow", "host=b"})) == 15.0);
    }

    SECTION("Time range") {
        Query query = make_query("sensor", "temperature", OperationType::AVG);
        query.from = 100;
        query.to = 200;

        REQUIRE(engine.query(query) == 35.0);
    }

    SECTION("Same field in two datasets") {
        REQUIRE(engine.query(make_query("cpu", "temperature", OperationType::COUNT)) == 1.0);
        REQUIRE(engine.query(make_query("cpu", "temperature", OperationType::AVG)) == 1000.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MAX)) == 100.0);
    }

    SECTION("Const engine") {
        const Engine &const_engine = engine;

        REQUIRE(const_engine.query(make_query("sensor", "temperature", OperationType::COUNT)) == 7.0);
    }

    SECTION("Inserts between queries") {
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MAX, {"host=c"})) == std::nullopt);

        REQUIRE(insert_line(engine, "sensor,host=c temperature=500 500\n") == true);

        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MAX, {"host=c"})) == 500.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MAX)) == 500.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::COUNT)) == 8.0);
    }

    SECTION("Rejected lines between queries") {
        REQUIRE(insert_line(engine, "sensor,host=a temperature=abc 100\n") == false);
        REQUIRE(insert_line(engine, "sensor,host=a temperature=1,pressure=xyz 100\n") == false);

        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::COUNT)) == 7.0);
        REQUIRE(engine.query(make_query("sensor", "temperature", OperationType::MIN)) == -5.0);
    }
}

TEST_CASE("Engine query, no matching data", "[engine]") {
    Engine engine;
    fill_engine(engine);

    auto require_no_data = [&](Query query) {
        query.operation_type = OperationType::COUNT;
        REQUIRE(engine.query(query) == 0.0);
        query.operation_type = OperationType::MIN;
        REQUIRE(engine.query(query) == std::nullopt);
        query.operation_type = OperationType::MAX;
        REQUIRE(engine.query(query) == std::nullopt);
        query.operation_type = OperationType::AVG;
        REQUIRE(engine.query(query) == std::nullopt);
    };

    SECTION("Unknown dataset") {
        require_no_data(make_query("disk", "temperature", OperationType::COUNT));
    }

    SECTION("Empty dataset name") {
        require_no_data(make_query("", "temperature", OperationType::COUNT));
    }

    SECTION("Unknown field") {
        require_no_data(make_query("sensor", "humidity", OperationType::COUNT));
    }

    SECTION("Field that exists only in another dataset") {
        REQUIRE(insert_line(engine, "cpu usage=5 1\n") == true);

        require_no_data(make_query("sensor", "usage", OperationType::COUNT));
    }

    SECTION("Unknown tag") {
        require_no_data(make_query("sensor", "temperature", OperationType::COUNT, {"loc=Gdansk"}));
    }

    SECTION("Tag that exists only in another dataset") {
        REQUIRE(insert_line(engine, "cpu,region=eu usage=5 1\n") == true);

        require_no_data(make_query("sensor", "temperature", OperationType::COUNT, {"region=eu"}));
    }

    SECTION("Tags that never appear together") {
        require_no_data(make_query("sensor", "temperature", OperationType::COUNT, {"loc=Warsaw", "host=b"}));
    }

    SECTION("Empty time range") {
        Query query = make_query("sensor", "temperature", OperationType::COUNT);
        query.from = 200;
        query.to = 200;

        require_no_data(query);
    }

    SECTION("Time range without data") {
        Query query = make_query("sensor", "temperature", OperationType::COUNT);
        query.from = 1000;
        query.to = 2000;

        require_no_data(query);
    }

    SECTION("Tag and field that never appear together") {
        require_no_data(make_query("sensor", "pressure", OperationType::COUNT, {"host=b"}));
    }

    SECTION("Empty engine") {
        Engine empty;

        Query query = make_query("sensor", "temperature", OperationType::COUNT);
        REQUIRE(empty.query(query) == 0.0);
        query.operation_type = OperationType::AVG;
        REQUIRE(empty.query(query) == std::nullopt);
    }

    SECTION("Dataset count after querying an unknown dataset") {
        size_t before = engine.dataset_count();

        engine.query(make_query("disk", "temperature", OperationType::COUNT));

        REQUIRE(engine.dataset_count() == before);
        REQUIRE(engine.find_mem_table("disk") == nullptr);
    }
}
