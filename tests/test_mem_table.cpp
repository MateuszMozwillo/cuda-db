#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include "db/parser.hpp"
#include "db/mem_table.hpp"

using namespace db;

static bool insert_line(MemTable &mem_table, const char* input) {
    DataPoint point;
    REQUIRE(parse_line(input, point) == true);
    return mem_table.insert(point);
}

static u_int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

TEST_CASE("MemTable insert", "[mem_table]") {
    MemTable mem_table;

    SECTION("Single field line") {
        bool success = insert_line(mem_table, "sensor,location=Krakow temperature=80.5 1727034041000\n");

        REQUIRE(success == true);
        REQUIRE(mem_table.row_count() == 1);
        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.field_ids()[0] == 1);
        REQUIRE(mem_table.field_values()[0] == 80.5);
        REQUIRE(mem_table.timestamps()[0] == 1727034041000);
    }

    SECTION("Multiple fields are stored in order with shared series and timestamp") {
        bool success = insert_line(mem_table, "sensor,location=Krakow temperature=80.5,pressure=1024.1,humidity=40.2 1727034041000\n");

        REQUIRE(success == true);
        REQUIRE(mem_table.row_count() == 3);

        REQUIRE(mem_table.field_values()[0] == 80.5);
        REQUIRE(mem_table.field_values()[1] == 1024.1);
        REQUIRE(mem_table.field_values()[2] == 40.2);

        REQUIRE(mem_table.field_ids()[0] == 1);
        REQUIRE(mem_table.field_ids()[1] == 2);
        REQUIRE(mem_table.field_ids()[2] == 3);

        for (size_t i = 0; i < 3; ++i) {
            REQUIRE(mem_table.series_ids()[i] == 1);
            REQUIRE(mem_table.timestamps()[i] == 1727034041000);
        }
    }

    SECTION("Missing timestamp uses current time") {
        u_int64_t before = now_ns();
        bool success = insert_line(mem_table, "cpu,host=server01 usage=99.9\n");
        u_int64_t after = now_ns();

        REQUIRE(success == true);
        REQUIRE(mem_table.row_count() == 1);
        REQUIRE(mem_table.timestamps()[0] >= before);
        REQUIRE(mem_table.timestamps()[0] <= after);
    }

    SECTION("Negative field value") {
        bool success = insert_line(mem_table, "sensor temperature=-12.5 1727034041000\n");

        REQUIRE(success == true);
        REQUIRE(mem_table.field_values()[0] == -12.5);
    }

    SECTION("Reject timestamp with trailing garbage") {
        bool success = insert_line(mem_table, "cpu usage=99.9 123abc\n");

        REQUIRE(success == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("Reject non-numeric timestamp") {
        bool success = insert_line(mem_table, "cpu usage=99.9 abc\n");

        REQUIRE(success == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("Reject timestamp overflowing u_int64") {
        bool success = insert_line(mem_table, "cpu usage=99.9 99999999999999999999999\n");

        REQUIRE(success == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("Reject field value with trailing garbage") {
        bool success = insert_line(mem_table, "cpu usage=12abc\n");

        REQUIRE(success == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("Reject non-numeric field values") {
        REQUIRE(insert_line(mem_table, "cpu usage=abc\n") == false);
        REQUIRE(insert_line(mem_table, "app_log msg=\"hello\"\n") == false);
        REQUIRE(insert_line(mem_table, "cpu ok=true\n") == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("Reject integer field with i suffix (not supported yet)") {
        bool success = insert_line(mem_table, "cpu usage_user=58i\n");

        REQUIRE(success == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("One invalid field rejects the whole line") {
        bool success = insert_line(mem_table, "sensor temperature=80.5,pressure=abc,humidity=40.2 1727034041000\n");

        REQUIRE(success == false);
        REQUIRE(mem_table.row_count() == 0);
    }

    SECTION("Rejected line does not allocate series or field ids") {
        REQUIRE(insert_line(mem_table, "sensor,location=Warsaw temperature=80.5,pressure=abc 1727034041000\n") == false);
        REQUIRE(insert_line(mem_table, "sensor,location=Gdansk humidity=40.2 abc\n") == false);

        bool success = insert_line(mem_table, "sensor,location=Krakow wind=3.5 1727034041000\n");

        REQUIRE(success == true);
        REQUIRE(mem_table.row_count() == 1);
        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.field_ids()[0] == 1);
    }
}

TEST_CASE("MemTable series ids", "[mem_table]") {
    MemTable mem_table;

    SECTION("Same tags give the same series id") {
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow temperature=81.0 2\n") == true);

        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.series_ids()[1] == 1);
    }

    SECTION("Different tag values give consecutive series ids") {
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Warsaw temperature=81.0 2\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Gdansk temperature=82.0 3\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Warsaw temperature=83.0 4\n") == true);

        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.series_ids()[1] == 2);
        REQUIRE(mem_table.series_ids()[2] == 3);
        REQUIRE(mem_table.series_ids()[3] == 2);
    }

    SECTION("Line without tags gets a stable series id") {
        REQUIRE(insert_line(mem_table, "sensor temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow temperature=81.0 2\n") == true);
        REQUIRE(insert_line(mem_table, "sensor temperature=82.0 3\n") == true);

        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.series_ids()[1] == 2);
        REQUIRE(mem_table.series_ids()[2] == 1);
    }

    SECTION("Additional tag creates a new series") {
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow,version=2 temperature=81.0 2\n") == true);

        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.series_ids()[1] == 2);
    }

    SECTION("Escaped spaces in tag values") {
        REQUIRE(insert_line(mem_table, "sensor,location=New\\ York temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=New\\ York temperature=81.0 2\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=New temperature=82.0 3\n") == true);

        REQUIRE(mem_table.series_ids()[0] == 1);
        REQUIRE(mem_table.series_ids()[1] == 1);
        REQUIRE(mem_table.series_ids()[2] == 2);
    }

    SECTION("Tag order matters (current behaviour, update when tags are sorted)") {
        REQUIRE(insert_line(mem_table, "sensor,a=1,b=2 temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,b=2,a=1 temperature=81.0 2\n") == true);

        REQUIRE(mem_table.series_ids()[0] != mem_table.series_ids()[1]);
    }
}

TEST_CASE("MemTable field ids", "[mem_table]") {
    MemTable mem_table;

    SECTION("Same field name gives the same id across lines and series") {
        REQUIRE(insert_line(mem_table, "sensor,location=Krakow temperature=80.5,pressure=1024.1 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor,location=Warsaw pressure=1013.2,temperature=20.1 2\n") == true);

        REQUIRE(mem_table.field_ids()[0] == 1);
        REQUIRE(mem_table.field_ids()[1] == 2);
        REQUIRE(mem_table.field_ids()[2] == 2);
        REQUIRE(mem_table.field_ids()[3] == 1);
    }

    SECTION("New field names get consecutive ids") {
        REQUIRE(insert_line(mem_table, "sensor temperature=80.5 1\n") == true);
        REQUIRE(insert_line(mem_table, "sensor humidity=40.2 2\n") == true);
        REQUIRE(insert_line(mem_table, "sensor wind=3.5,temperature=81.0 3\n") == true);

        REQUIRE(mem_table.field_ids()[0] == 1);
        REQUIRE(mem_table.field_ids()[1] == 2);
        REQUIRE(mem_table.field_ids()[2] == 3);
        REQUIRE(mem_table.field_ids()[3] == 1);
    }

    SECTION("Many distinct field names do not wrap around") {
        for (unsigned int i = 0; i < 300; ++i) {
            std::string input = "sensor field" + std::to_string(i) + "=1.0 1\n";
            REQUIRE(insert_line(mem_table, input.c_str()) == true);
        }

        REQUIRE(mem_table.row_count() == 300);
        REQUIRE(mem_table.field_ids()[299] == 300);
    }
}
