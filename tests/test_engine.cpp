#include <catch2/catch_test_macros.hpp>
#include <string>
#include "db/parser.hpp"
#include "db/engine.hpp"

using namespace db;

static bool insert_line(Engine &engine, const char* input) {
    DataPoint point;
    REQUIRE(parse_line(input, point) == true);
    return engine.insert(point);
}

TEST_CASE("Engine routes data points to mem tables", "[engine]") {
    Engine engine;

    SECTION("Lines from the same dataset go to one mem table") {
        REQUIRE(insert_line(engine, "sensor,location=Krakow temperature=80.5 1\n") == true);
        REQUIRE(insert_line(engine, "sensor,location=Warsaw temperature=81.0 2\n") == true);

        REQUIRE(engine.dataset_count() == 1);

        const MemTable *sensor = engine.find_mem_table("sensor");
        REQUIRE(sensor != nullptr);
        REQUIRE(sensor->row_count() == 2);
    }

    SECTION("Different datasets get separate mem tables") {
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

    SECTION("Series and field ids start from 1 in every dataset") {
        REQUIRE(insert_line(engine, "sensor,location=Krakow temperature=80.5,pressure=1024.1 1\n") == true);
        REQUIRE(insert_line(engine, "cpu,host=server01 usage=42.5 2\n") == true);

        const MemTable *cpu = engine.find_mem_table("cpu");
        REQUIRE(cpu != nullptr);
        REQUIRE(cpu->series_ids()[0] == 1);
        REQUIRE(cpu->field_ids()[0] == 1);
    }

    SECTION("Unknown dataset is not found") {
        REQUIRE(insert_line(engine, "sensor temperature=80.5 1\n") == true);

        REQUIRE(engine.find_mem_table("cpu") == nullptr);
    }

    SECTION("Rejected line does not create a mem table") {
        REQUIRE(insert_line(engine, "sensor temperature=abc 1\n") == false);

        REQUIRE(engine.dataset_count() == 0);
        REQUIRE(engine.find_mem_table("sensor") == nullptr);
    }

    SECTION("Data survives mem_tables vector reallocation") {
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
