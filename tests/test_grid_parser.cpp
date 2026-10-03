#include <catch2/catch_test_macros.hpp>
import grid_parser;

TEST_CASE("grid_parser: parse_log_line parses valid line", "[grid_parser]") {
    std::string_view line = "2026-10-01T08:00:00 [INFO] Substation_A initialization complete.";
    auto entry = parse_log_line(line);
    
    REQUIRE(entry.timestamp == "2026-10-01T08:00:00");
    REQUIRE(entry.severity == Severity::INFO);
    REQUIRE(entry.message == "Substation_A initialization complete.");
}

TEST_CASE("grid_parser: parse_log_line parses WARN severity", "[grid_parser]") {
    std::string_view line = "2026-10-01T08:05:12 [WARN] Bus_12 voltage dropping (0.94 pu).";
    auto entry = parse_log_line(line);
    
    REQUIRE(entry.severity == Severity::WARN);
    REQUIRE(entry.message == "Bus_12 voltage dropping (0.94 pu).");
}

TEST_CASE("grid_parser: parse_log_line parses CRITICAL severity", "[grid_parser]") {
    std::string_view line = "2026-10-01T08:07:45 [CRITICAL] Transformer_T1 overloaded (115%).";
    auto entry = parse_log_line(line);
    
    REQUIRE(entry.severity == Severity::CRITICAL);
}

TEST_CASE("grid_parser: parse_log_line handles unknown severity", "[grid_parser]") {
    std::string_view line = "2026-10-01T08:00:00 [DEBUG] Some debug message.";
    auto entry = parse_log_line(line);
    
    REQUIRE(entry.severity == Severity::UNKNOWN);
    REQUIRE(entry.message == "Some debug message.");
}

TEST_CASE("grid_parser: parse_log_line handles malformed line", "[grid_parser]") {
    std::string_view line = "not a valid log line";
    auto entry = parse_log_line(line);
    
    REQUIRE(entry.severity == Severity::UNKNOWN);
    REQUIRE(entry.message == "not a valid log line");
}

TEST_CASE("grid_parser: parse_log_line handles empty message", "[grid_parser]") {
    std::string_view line = "2026-10-01T08:00:00 [INFO] ";
    auto entry = parse_log_line(line);
    
    REQUIRE(entry.severity == Severity::INFO);
    REQUIRE(entry.message.empty());
}

TEST_CASE("grid_parser: enum values match log_parser.hpp", "[grid_parser][compat]") {
    // Ensure both parsers use same severity vocabulary
    static_assert(static_cast<int>(Severity::INFO) == 0);
    static_assert(static_cast<int>(Severity::WARN) == 1);
    static_assert(static_cast<int>(Severity::CRITICAL) == 2);
    static_assert(static_cast<int>(Severity::UNKNOWN) == 3);
}