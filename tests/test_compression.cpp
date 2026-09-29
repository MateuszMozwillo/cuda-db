#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>
#include "db/compression.hpp"

using namespace db;

static constexpr std::uint64_t U64_MAX = std::numeric_limits<std::uint64_t>::max();

static void require_round_trip(const std::vector<std::uint64_t> &values) {
    PackedColumn column = pack_column(values);

    REQUIRE(column.row_count == values.size());
    REQUIRE(column.blocks.size() == (values.size() + PACKED_BLOCK_ROWS - 1) / PACKED_BLOCK_ROWS);
    REQUIRE(column.words.back() == 0);
    REQUIRE(unpack_column(column) == values);
}

static void require_unpack_value_matches(const std::vector<std::uint64_t> &values) {
    PackedColumn column = pack_column(values);
    PackedColumnView view{column.blocks.data(), column.words.data()};

    for (std::size_t row = 0; row < values.size(); ++row) {
        INFO("row " << row);
        REQUIRE(unpack_value(view, row) == values[row]);
    }
}

static std::vector<std::uint64_t> random_values(std::size_t count, std::uint64_t min, std::uint64_t max, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::uint64_t> dist(min, max);
    std::vector<std::uint64_t> values(count);
    for (auto &value : values) value = dist(rng);
    return values;
}

static std::vector<std::uint64_t> timestamps(std::size_t count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::uint64_t> step(1, 1000);
    std::vector<std::uint64_t> values(count);
    std::uint64_t ts = 1'700'000'000'000'000'000ULL;
    for (auto &value : values) {
        ts += step(rng);
        value = ts;
    }
    return values;
}

TEST_CASE("pack_column round trip", "[compression]") {
    SECTION("Empty column") {
        PackedColumn column = pack_column(std::vector<std::uint64_t>{});

        REQUIRE(column.row_count == 0);
        REQUIRE(column.blocks.empty());
        REQUIRE(column.words.size() == 1);
        REQUIRE(unpack_column(column).empty());
    }

    SECTION("Single value") {
        require_round_trip({42});
    }

    SECTION("Single zero") {
        require_round_trip({0});
    }

    SECTION("Single maximum value") {
        require_round_trip({U64_MAX});
    }

    SECTION("All values equal") {
        require_round_trip(std::vector<std::uint64_t>(5000, 123456789));
    }

    SECTION("Exactly one block") {
        require_round_trip(random_values(PACKED_BLOCK_ROWS, 0, 1000, 1));
    }

    SECTION("One value more than a block") {
        require_round_trip(random_values(PACKED_BLOCK_ROWS + 1, 0, 1000, 2));
    }

    SECTION("One value less than a block") {
        require_round_trip(random_values(PACKED_BLOCK_ROWS - 1, 0, 1000, 3));
    }

    SECTION("Many blocks with a partial last block") {
        require_round_trip(random_values(PACKED_BLOCK_ROWS * 7 + 333, 0, 5000, 4));
    }

    SECTION("Full 64 bit range") {
        require_round_trip({0, U64_MAX, 1, U64_MAX - 1, 12345});
        require_round_trip(random_values(10000, 0, U64_MAX, 5));
    }

    SECTION("Values near the maximum") {
        require_round_trip(random_values(3000, U64_MAX - 1000, U64_MAX, 6));
    }

    SECTION("Every bit width from 1 to 64") {
        for (unsigned width = 1; width <= 64; ++width) {
            INFO("bit width " << width);
            std::uint64_t max_difference = width == 64 ? U64_MAX : (1ULL << width) - 1;
            std::vector<std::uint64_t> values = random_values(PACKED_BLOCK_ROWS + 77, 0, max_difference, width);
            values[0] = 0;
            values[1] = max_difference;

            require_round_trip(values);
            REQUIRE(pack_column(values).blocks[0].bit_width == width);
        }
    }

    SECTION("Timestamps") {
        require_round_trip(timestamps(100000, 7));
    }

    SECTION("Unsorted values") {
        require_round_trip({500, 3, 999, 0, 42, 1000, 7});
    }

    SECTION("32 bit column") {
        std::vector<std::uint32_t> values = {7, 0, 300, 4294967295u, 12, 299};
        PackedColumn column = pack_column(values);

        std::vector<std::uint64_t> expected(values.begin(), values.end());
        REQUIRE(unpack_column(column) == expected);
    }
}

TEST_CASE("pack_column block metadata and size", "[compression]") {
    SECTION("Block whose first value is not the minimum") {
        PackedColumn column = pack_column(std::vector<std::uint64_t>{900, 137, 150});

        REQUIRE(column.blocks[0].base == 137);
        REQUIRE(column.blocks[0].bit_width == 10);
    }

    SECTION("Block of equal values") {
        PackedColumn column = pack_column(std::vector<std::uint64_t>(PACKED_BLOCK_ROWS, 5));

        REQUIRE(column.blocks[0].bit_width == 0);
        REQUIRE(column.words.size() == 1);
    }

    SECTION("Two blocks with different ranges") {
        std::vector<std::uint64_t> values(PACKED_BLOCK_ROWS * 2);
        for (std::size_t i = 0; i < PACKED_BLOCK_ROWS; ++i) values[i] = 1000 + (i % 2);
        for (std::size_t i = PACKED_BLOCK_ROWS; i < values.size(); ++i) values[i] = 5000000 + i * 1000;

        PackedColumn column = pack_column(values);

        REQUIRE(column.blocks[0].base == 1000);
        REQUIRE(column.blocks[0].bit_width == 1);
        REQUIRE(column.blocks[1].base == 5000000 + PACKED_BLOCK_ROWS * 1000);
        REQUIRE(column.blocks[1].word_offset > column.blocks[0].word_offset);
        REQUIRE(unpack_column(column) == values);
    }

    SECTION("Timestamps with steps of 1 to 1000") {
        std::vector<std::uint64_t> values = timestamps(PACKED_BLOCK_ROWS * 100, 8);
        PackedColumn column = pack_column(values);

        for (const PackedBlock &block : column.blocks) {
            REQUIRE(block.bit_width <= 20);
        }

        std::size_t raw_bytes = values.size() * sizeof(std::uint64_t);
        REQUIRE(column.size_bytes() * 3 < raw_bytes);
    }
}

TEST_CASE("unpack_value", "[compression][unpack_value]") {
    SECTION("Small values") {
        require_unpack_value_matches({500, 3, 999, 0, 42, 1000, 7});
    }

    SECTION("All values equal") {
        require_unpack_value_matches(std::vector<std::uint64_t>(3000, 77));
    }

    SECTION("Values crossing block boundaries") {
        require_unpack_value_matches(random_values(PACKED_BLOCK_ROWS * 3 + 5, 0, 100000, 9));
    }

    SECTION("Every bit width from 1 to 64") {
        for (unsigned width = 1; width <= 64; ++width) {
            INFO("bit width " << width);
            std::uint64_t max_difference = width == 64 ? U64_MAX : (1ULL << width) - 1;
            std::vector<std::uint64_t> values = random_values(PACKED_BLOCK_ROWS + 77, 0, max_difference, 100 + width);
            values[0] = 0;
            values[1] = max_difference;

            require_unpack_value_matches(values);
        }
    }

    SECTION("Full 64 bit range") {
        require_unpack_value_matches({0, U64_MAX, 1, U64_MAX - 1, 12345});
    }

    SECTION("Timestamps") {
        require_unpack_value_matches(timestamps(20000, 10));
    }
}
