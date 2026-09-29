#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include "db/query.hpp"

using namespace db;

static constexpr double INF = std::numeric_limits<double>::infinity();

static AggState state_of(const std::vector<double> &values) {
    AggState state;
    for (double value : values) {
        state.update(value);
    }
    return state;
}

static void require_same(const AggState &a, const AggState &b) {
    REQUIRE(a.count == b.count);
    REQUIRE(a.sum == b.sum);
    REQUIRE(a.min == b.min);
    REQUIRE(a.max == b.max);
}

TEST_CASE("AggState update", "[aggregation]") {
    SECTION("Empty state") {
        AggState state;

        REQUIRE(state.count == 0);
        REQUIRE(state.sum == 0.0);
        REQUIRE(state.min == INF);
        REQUIRE(state.max == -INF);
    }

    SECTION("Single value") {
        AggState state = state_of({42.5});

        REQUIRE(state.count == 1);
        REQUIRE(state.sum == 42.5);
        REQUIRE(state.min == 42.5);
        REQUIRE(state.max == 42.5);
    }

    SECTION("Several values") {
        AggState state = state_of({10.0, 30.0, 20.0});

        REQUIRE(state.count == 3);
        REQUIRE(state.sum == 60.0);
        REQUIRE(state.min == 10.0);
        REQUIRE(state.max == 30.0);
    }

    SECTION("Only negative values") {
        AggState state = state_of({-5.0, -3.0, -10.0});

        REQUIRE(state.count == 3);
        REQUIRE(state.sum == -18.0);
        REQUIRE(state.min == -10.0);
        REQUIRE(state.max == -3.0);
    }

    SECTION("Only positive values") {
        AggState state = state_of({5.0, 3.0, 10.0});

        REQUIRE(state.min == 3.0);
        REQUIRE(state.max == 10.0);
    }

    SECTION("Zero as the only value") {
        AggState state = state_of({0.0});

        REQUIRE(state.count == 1);
        REQUIRE(state.sum == 0.0);
        REQUIRE(state.min == 0.0);
        REQUIRE(state.max == 0.0);
    }

    SECTION("Repeated identical values") {
        AggState state = state_of({7.0, 7.0, 7.0, 7.0});

        REQUIRE(state.count == 4);
        REQUIRE(state.sum == 28.0);
        REQUIRE(state.min == 7.0);
        REQUIRE(state.max == 7.0);
    }

    SECTION("Values that cancel out") {
        AggState state = state_of({1e6, -1e6});

        REQUIRE(state.count == 2);
        REQUIRE(state.sum == 0.0);
        REQUIRE(state.min == -1e6);
        REQUIRE(state.max == 1e6);
    }

    SECTION("Extreme finite values") {
        double lowest = std::numeric_limits<double>::lowest();
        double highest = std::numeric_limits<double>::max();
        AggState state = state_of({lowest, highest});

        REQUIRE(state.min == lowest);
        REQUIRE(state.max == highest);
    }

    SECTION("Smallest positive subnormal value") {
        double tiny = std::numeric_limits<double>::denorm_min();
        AggState state = state_of({tiny});

        REQUIRE(state.min == tiny);
        REQUIRE(state.max == tiny);
        REQUIRE(state.sum == tiny);
    }

    SECTION("Two maximum finite values") {
        double highest = std::numeric_limits<double>::max();
        AggState state = state_of({highest, highest});

        REQUIRE(state.count == 2);
        REQUIRE(std::isinf(state.sum));
        REQUIRE(state.max == highest);
    }

    SECTION("0.1 added ten times") {
        AggState state;
        for (int i = 0; i < 10; ++i) {
            state.update(0.1);
        }

        REQUIRE(state.count == 10);
        REQUIRE(state.sum == Catch::Approx(1.0));
    }
}

TEST_CASE("AggState merge", "[aggregation]") {
    SECTION("Empty with empty") {
        AggState a;
        a.merge(AggState{});

        require_same(a, AggState{});
    }

    SECTION("Empty merged into a state") {
        AggState a = state_of({1.0, -2.0, 3.0});
        AggState expected = a;

        a.merge(AggState{});

        require_same(a, expected);
    }

    SECTION("State merged into empty") {
        AggState a;
        AggState b = state_of({1.0, -2.0, 3.0});

        a.merge(b);

        require_same(a, b);
    }

    SECTION("Two states with three values each") {
        AggState a = state_of({4.0, -8.0, 15.0});
        AggState b = state_of({16.0, 23.0, -42.0});

        a.merge(b);

        require_same(a, state_of({4.0, -8.0, 15.0, 16.0, 23.0, -42.0}));
    }

    SECTION("Two states merged in both orders") {
        AggState a = state_of({1.0, 5.0});
        AggState b = state_of({-3.0, 9.0, 2.0});

        AggState ab = a;
        ab.merge(b);
        AggState ba = b;
        ba.merge(a);

        require_same(ab, ba);
    }

    SECTION("Three states merged in two groupings") {
        AggState a = state_of({1.0, 2.0});
        AggState b = state_of({-7.0});
        AggState c = state_of({100.0, -50.0});

        AggState left = a;
        left.merge(b);
        left.merge(c);

        AggState bc = b;
        bc.merge(c);
        AggState right = a;
        right.merge(bc);

        require_same(left, right);
    }

    SECTION("101 single value states") {
        std::vector<double> values;
        AggState merged;
        for (int i = -50; i <= 50; ++i) {
            values.push_back(static_cast<double>(i));
            merged.merge(state_of({static_cast<double>(i)}));
        }

        require_same(merged, state_of(values));
        REQUIRE(merged.count == 101);
        REQUIRE(merged.sum == 0.0);
        REQUIRE(merged.min == -50.0);
        REQUIRE(merged.max == 50.0);
    }

    SECTION("State merged with itself") {
        AggState a = state_of({2.0, -4.0, 6.0});

        a.merge(a);

        REQUIRE(a.count == 6);
        REQUIRE(a.sum == 8.0);
        REQUIRE(a.min == -4.0);
        REQUIRE(a.max == 6.0);
    }

    SECTION("States with only negative values") {
        AggState a = state_of({-10.0, -20.0});
        AggState b = state_of({-5.0});

        a.merge(b);

        REQUIRE(a.min == -20.0);
        REQUIRE(a.max == -5.0);
    }
}

TEST_CASE("finalize", "[aggregation]") {
    SECTION("COUNT of empty state") {
        std::optional<double> result = finalize(AggState{}, OperationType::COUNT);

        REQUIRE(result.has_value());
        REQUIRE(*result == 0.0);
    }

    SECTION("MIN, MAX and AVG of empty state") {
        REQUIRE(finalize(AggState{}, OperationType::MIN) == std::nullopt);
        REQUIRE(finalize(AggState{}, OperationType::MAX) == std::nullopt);
        REQUIRE(finalize(AggState{}, OperationType::AVG) == std::nullopt);
    }

    SECTION("Single value") {
        AggState state = state_of({-3.5});

        REQUIRE(finalize(state, OperationType::COUNT) == 1.0);
        REQUIRE(finalize(state, OperationType::MIN) == -3.5);
        REQUIRE(finalize(state, OperationType::MAX) == -3.5);
        REQUIRE(finalize(state, OperationType::AVG) == -3.5);
    }

    SECTION("Several values") {
        AggState state = state_of({10.0, 20.0, 60.0});

        REQUIRE(finalize(state, OperationType::COUNT) == 3.0);
        REQUIRE(finalize(state, OperationType::MIN) == 10.0);
        REQUIRE(finalize(state, OperationType::MAX) == 60.0);
        REQUIRE(finalize(state, OperationType::AVG) == 30.0);
    }

    SECTION("AVG of 1 and 2") {
        AggState state = state_of({1.0, 2.0});

        REQUIRE(finalize(state, OperationType::AVG) == 1.5);
    }

    SECTION("AVG of values that cancel out") {
        AggState state = state_of({-5.0, 5.0});

        REQUIRE(finalize(state, OperationType::AVG) == 0.0);
    }

    SECTION("AVG of repeating fraction") {
        AggState state = state_of({1.0, 1.0, 2.0});

        std::optional<double> result = finalize(state, OperationType::AVG);
        REQUIRE(result.has_value());
        REQUIRE(*result == Catch::Approx(4.0 / 3.0));
    }

    SECTION("AVG of merged states of different sizes") {
        AggState a = state_of({10.0});
        AggState b = state_of({20.0, 20.0, 20.0});

        a.merge(b);

        REQUIRE(finalize(a, OperationType::AVG) == 17.5);
    }

    SECTION("Unknown operation type") {
        AggState state = state_of({1.0});
        OperationType unknown = static_cast<OperationType>(42);

        REQUIRE(finalize(state, unknown) == std::nullopt);
        REQUIRE(finalize(AggState{}, unknown) == std::nullopt);
    }
}
