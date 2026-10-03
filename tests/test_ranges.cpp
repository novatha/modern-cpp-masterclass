#include <catch2/catch_test_macros.hpp>
#include "phase4_pipeline.hpp"

#include <string>
#include <vector>

using namespace grid_telemetry;

namespace {
// The same simulated PMU timeseries used by the mini_project_4 demo.
std::vector<SensorReading> sample_readings() {
    return {
        {0,    1.00, true},
        {100,  0.99, true},
        {200, -99.9, false}, // Sensor glitch (invalid)
        {300,  0.97, true},
        {400,  0.92, true},  // Voltage drop!
        {500,  0.00, false}, // Network packet lost
        {600,  0.88, true},  // Critical drop!
        {700,  0.85, true}   // Another drop
    };
}
} // namespace

TEST_CASE("critical_drop_alerts: filters out invalid readings", "[ranges]") {
    auto alerts = critical_drop_alerts(sample_readings(), 10);

    // Glitched (time 200) and packet-lost (time 500) readings must never appear
    for (const auto& alert : alerts) {
        REQUIRE(alert.find("200") == std::string::npos);
        REQUIRE(alert.find("500") == std::string::npos);
    }
}

TEST_CASE("critical_drop_alerts: keeps only voltage below 0.95 pu", "[ranges]") {
    // Valid readings below 0.95 pu: time 400 (0.92), 600 (0.88), 700 (0.85)
    auto alerts = critical_drop_alerts(sample_readings(), 10);
    REQUIRE(alerts.size() == 3);
}

TEST_CASE("critical_drop_alerts: take() limits the pipeline output", "[ranges]") {
    auto alerts = critical_drop_alerts(sample_readings(), 2);
    REQUIRE(alerts.size() == 2);
    REQUIRE(alerts[0].find("400") != std::string::npos);
    REQUIRE(alerts[1].find("600") != std::string::npos);
}

TEST_CASE("critical_drop_alerts: formats each alert string", "[ranges]") {
    auto alerts = critical_drop_alerts(sample_readings(), 1);
    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0] == "Time 400ms: CRITICAL VOLTAGE 0.92 pu");
}

TEST_CASE("critical_drop_alerts: empty input yields no alerts", "[ranges]") {
    auto alerts = critical_drop_alerts({}, 2);
    REQUIRE(alerts.empty());
}

TEST_CASE("find_first_drop: returns the first extreme drop time", "[optional]") {
    // First VALID reading below 0.90 pu is time 600 (0.88 pu)
    auto drop_time = find_first_drop(sample_readings());
    REQUIRE(drop_time.has_value());
    REQUIRE(drop_time.value() == 600);
}

TEST_CASE("find_first_drop: returns nullopt when nothing matches", "[optional]") {
    std::vector<SensorReading> healthy = {
        {0, 1.00, true},
        {100, 0.99, true},
        {200, 0.96, true},
    };
    REQUIRE_FALSE(find_first_drop(healthy).has_value());
}

TEST_CASE("find_first_drop: ignores invalid readings even if extreme", "[optional]") {
    std::vector<SensorReading> glitchy = {
        {0, 0.50, false}, // Extreme voltage, but sensor is invalid
        {100, 1.00, true},
    };
    REQUIRE_FALSE(find_first_drop(glitchy).has_value());
}

TEST_CASE("find_first_drop: if-with-initializer idiom", "[optional]") {
    // The C++17 "if with initializer" pattern used in the demo
    if (auto drop_time = find_first_drop(sample_readings()); drop_time.has_value()) {
        REQUIRE(drop_time.value() == 600);
    } else {
        FAIL("Expected a drop at 600ms");
    }
}
