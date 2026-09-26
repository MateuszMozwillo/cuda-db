#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include "db/parser.hpp"

using namespace db;

TEST_CASE("InfluxDB Line Protocol Parser", "[parser]") {
    DataPoint point;

    SECTION("Valid line with all components") {
        const char* input = "sensor,location=Krakow temperature=80.5 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "sensor");
        REQUIRE(point.tag_count == 1);
        REQUIRE(point.tags[0].key == "location");
        REQUIRE(point.tags[0].value == "Krakow");
        
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].key == "temperature");
        REQUIRE(point.fields[0].value == "80.5");
        
        REQUIRE(point.timestamp == "1727034041000");
    }

    SECTION("Valid line without timestamp") {
        const char* input = "cpu,host=server01 usage=99.9\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "cpu");
        REQUIRE(point.timestamp.empty() == true);
        REQUIRE(point.field_count == 1);
    }

    SECTION("Malformed line without fields") {
        const char* input = "random_data_that_wont_work\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Multiple tags and fields") {
        const char* input = "disk,host=server01,region=eu-central usage=85.5,free_gb=15.2 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "disk");
        
        REQUIRE(point.tag_count == 2);
        REQUIRE(point.tags[0].key == "host");
        REQUIRE(point.tags[0].value == "server01");
        REQUIRE(point.tags[1].key == "region");
        REQUIRE(point.tags[1].value == "eu-central");

        REQUIRE(point.field_count == 2);
        REQUIRE(point.fields[0].key == "usage");
        REQUIRE(point.fields[0].value == "85.5");
        REQUIRE(point.fields[1].key == "free_gb");
        REQUIRE(point.fields[1].value == "15.2");
        
        REQUIRE(point.timestamp == "1727034041000");
    }

    SECTION("Reject empty line") {
        const char* input = "\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject missing field value") {
        const char* input = "cpu,host=server01 usage=\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Valid line with extra spaces before timestamp") {
        const char* input = "cpu usage=42.0    1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "cpu");
        REQUIRE(point.field_count == 1);
        REQUIRE(point.timestamp == "1727034041000");
    }

    SECTION("Valid line with CRLF (Windows line endings)") {
        const char* input = "cpu,host=server01 usage=99.9\r\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "cpu");
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].key == "usage");
        REQUIRE(point.fields[0].value == "99.9");
        REQUIRE(point.timestamp.empty() == true);
    }

    SECTION("Reject line with only measurement (no fields, no tags)") {
        const char* input = "cpu\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject line with missing measurement name") {
        const char* input = ",host=server01 usage=99.9\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject line with missing field key") {
        const char* input = "cpu,host=server01 =99.9\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject line with trailing comma in tags") {
        const char* input = "cpu,host=server01, usage=99.9\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Valid line with escaped spaces in tags") {
        const char* input = "cpu,host=server\\ 01 usage=99.9\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "cpu");
        REQUIRE(point.tag_count == 1);
        REQUIRE(point.tags[0].key == "host");
        REQUIRE(point.tags[0].value == "server\\ 01"); 
        REQUIRE(point.field_count == 1);
    }

    SECTION("Valid line without newline at the very end of file") {
        const char* input = "cpu,host=server01 usage=99.9";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "cpu");
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].value == "99.9");
    }

    SECTION("Valid line with quoted string containing spaces") {
        const char* input = "app_log msg=\"fatal error occurred\" 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "app_log");
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].key == "msg");
        REQUIRE(point.fields[0].value == "\"fatal error occurred\""); 
        REQUIRE(point.timestamp == "1727034041000");
    }

    SECTION("Valid line with escaped quotes inside a quoted string") {
        const char* input = "app_log msg=\"hello \\\"world\\\"\" 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.dataset == "app_log");
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].key == "msg");
        REQUIRE(point.fields[0].value == "\"hello \\\"world\\\"\""); 
        REQUIRE(point.timestamp == "1727034041000");
    }

    SECTION("Reject line with unclosed quote") {
        const char* input = "app_log msg=\"this string never ends 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Multiple quoted fields and numeric fields mixed") {
        const char* input = "audit user=\"mati\",action=\"login\",attempts=3 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.field_count == 3);
        REQUIRE(point.fields[0].key == "user");
        REQUIRE(point.fields[0].value == "\"mati\"");
        REQUIRE(point.fields[1].key == "action");
        REQUIRE(point.fields[1].value == "\"login\"");
        REQUIRE(point.fields[2].key == "attempts");
        REQUIRE(point.fields[2].value == "3");
    }

    SECTION("Empty quoted string") {
        const char* input = "app_log msg=\"\" 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].value == "\"\""); 
    }

    SECTION("Reject line exceeding MAX_TAG_COUNT") {
        std::string input = "sensor";
        for (unsigned int i = 0; i <= MAX_TAG_COUNT; ++i) {
            input += ",tag" + std::to_string(i) + "=value";
        }
        input += " temp=80.5\n";

        bool success = parse_line(input.c_str(), point);
        
        REQUIRE(success == false); 
    }

    SECTION("Reject line exceeding MAX_FIELD_COUNT") {
        std::string input = "sensor,loc=Krakow ";
        for (unsigned int i = 0; i <= MAX_FIELD_COUNT; ++i) {
            if (i > 0) input += ",";
            input += "field" + std::to_string(i) + "=1.0";
        }
        input += "\n";

        bool success = parse_line(input.c_str(), point);

        REQUIRE(success == false);
    }

    SECTION("Reject unescaped space in tag key") {
        const char* input = "cpu,ho st=a u=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject unescaped comma in tag key") {
        const char* input = "cpu,host,x=1 u=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject unescaped equals sign in tag value") {
        const char* input = "cpu,a=b=c u=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject unescaped space in field key") {
        const char* input = "cpu a b=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject unescaped comma in field key") {
        const char* input = "cpu a,b=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject quote in the middle of unquoted field value") {
        const char* input = "cpu u=ab\"c d\"\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject characters after closing quote") {
        const char* input = "cpu u=\"abc\"x\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Reject characters after closing quote before next field") {
        const char* input = "cpu u=\"abc\"x,v=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == false);
    }

    SECTION("Valid line with escaped space in tag key") {
        const char* input = "cpu,ho\\ st=a u=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.tag_count == 1);
        REQUIRE(point.tags[0].key == "ho\\ st");
        REQUIRE(point.tags[0].value == "a");
    }

    SECTION("Valid line with escaped comma in tag key") {
        const char* input = "cpu,host\\,x=1 u=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.tag_count == 1);
        REQUIRE(point.tags[0].key == "host\\,x");
        REQUIRE(point.tags[0].value == "1");
    }

    SECTION("Valid line with escaped equals sign in tag value") {
        const char* input = "cpu,a=b\\=c u=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.tag_count == 1);
        REQUIRE(point.tags[0].key == "a");
        REQUIRE(point.tags[0].value == "b\\=c");
    }

    SECTION("Valid line with escaped space in field key") {
        const char* input = "cpu a\\ b=1\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.field_count == 1);
        REQUIRE(point.fields[0].key == "a\\ b");
        REQUIRE(point.fields[0].value == "1");
    }

    SECTION("Valid quoted field followed by another field") {
        const char* input = "cpu u=\"a b\",v=1 1727034041000\n";
        bool success = parse_line(input, point);

        REQUIRE(success == true);
        REQUIRE(point.field_count == 2);
        REQUIRE(point.fields[0].value == "\"a b\"");
        REQUIRE(point.fields[1].key == "v");
        REQUIRE(point.fields[1].value == "1");
        REQUIRE(point.timestamp == "1727034041000");
    }
}
