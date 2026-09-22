#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include "db/parser.hpp"

TEST_CASE("InfluxDB Line Protocol Parser", "[parser]") {
    std::string_view measurement;
    KeyValuePair tags[MAX_TAG_COUNT];
    unsigned int tag_count = 0;
    KeyValuePair fields[MAX_FIELD_COUNT];
    unsigned int field_count = 0;
    std::string_view timestamp;

    SECTION("Valid line with all components") {
        const char* input = "sensor,location=Krakow temperature=80.5 1727034041000\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "sensor");
        REQUIRE(tag_count == 1);
        REQUIRE(tags[0].key == "location");
        REQUIRE(tags[0].value == "Krakow");
        
        REQUIRE(field_count == 1);
        REQUIRE(fields[0].key == "temperature");
        REQUIRE(fields[0].value == "80.5");
        
        REQUIRE(timestamp == "1727034041000");
    }

    SECTION("Valid line without timestamp") {
        const char* input = "cpu,host=server01 usage=99.9\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "cpu");
        REQUIRE(timestamp.empty() == true);
        REQUIRE(field_count == 1);
    }

    SECTION("Malformed line without fields") {
        const char* input = "random_data_that_wont_work\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Multiple tags and fields") {
        const char* input = "disk,host=server01,region=eu-central usage=85.5,free_gb=15.2 1727034041000\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "disk");
        
        REQUIRE(tag_count == 2);
        REQUIRE(tags[0].key == "host");
        REQUIRE(tags[0].value == "server01");
        REQUIRE(tags[1].key == "region");
        REQUIRE(tags[1].value == "eu-central");

        REQUIRE(field_count == 2);
        REQUIRE(fields[0].key == "usage");
        REQUIRE(fields[0].value == "85.5");
        REQUIRE(fields[1].key == "free_gb");
        REQUIRE(fields[1].value == "15.2");
        
        REQUIRE(timestamp == "1727034041000");
    }

    SECTION("Reject empty line") {
        const char* input = "\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Reject missing field value") {
        const char* input = "cpu,host=server01 usage=\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Valid line with extra spaces before timestamp") {
        const char* input = "cpu usage=42.0    1727034041000\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "cpu");
        REQUIRE(field_count == 1);
        REQUIRE(timestamp == "1727034041000");
    }

    SECTION("Valid line with CRLF (Windows line endings)") {
        const char* input = "cpu,host=server01 usage=99.9\r\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "cpu");
        REQUIRE(field_count == 1);
        REQUIRE(fields[0].key == "usage");
        REQUIRE(fields[0].value == "99.9");
        REQUIRE(timestamp.empty() == true);
    }

    SECTION("Reject line with only measurement (no fields, no tags)") {
        const char* input = "cpu\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Reject line with missing measurement name") {
        const char* input = ",host=server01 usage=99.9\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Reject line with missing field key") {
        const char* input = "cpu,host=server01 =99.9\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Reject line with trailing comma in tags") {
        const char* input = "cpu,host=server01, usage=99.9\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == false);
    }

    SECTION("Valid line with escaped spaces in tags") {
        const char* input = "cpu,host=server\\ 01 usage=99.9\n";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "cpu");
        REQUIRE(tag_count == 1);
        REQUIRE(tags[0].key == "host");
        REQUIRE(tags[0].value == "server\\ 01"); 
        REQUIRE(field_count == 1);
    }

    SECTION("Valid line without newline at the very end of file") {
        const char* input = "cpu,host=server01 usage=99.9";
        bool success = parse_line(input, measurement, tags, tag_count, timestamp, fields, field_count);

        REQUIRE(success == true);
        REQUIRE(measurement == "cpu");
        REQUIRE(field_count == 1);
        REQUIRE(fields[0].value == "99.9");
    }
}