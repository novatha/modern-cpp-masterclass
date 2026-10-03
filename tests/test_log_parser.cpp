#include <catch2/catch_test_macros.hpp>
#include "log_parser.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

using namespace grid_logs;

TEST_CASE("parse_severity: maps known severities", "[log_parser]") {
    REQUIRE(parse_severity("INFO") == Severity::INFO);
    REQUIRE(parse_severity("WARN") == Severity::WARN);
    REQUIRE(parse_severity("CRITICAL") == Severity::CRITICAL);
}

TEST_CASE("parse_severity: unknown returns UNKNOWN", "[log_parser]") {
    REQUIRE(parse_severity("UNKNOWN") == Severity::UNKNOWN);
    REQUIRE(parse_severity("DEBUG") == Severity::UNKNOWN);
    REQUIRE(parse_severity("") == Severity::UNKNOWN);
}

TEST_CASE("parse_file: parses sample data correctly", "[log_parser]") {
    std::string path = std::string(DATA_DIR) + "/sample_grid_logs.txt";
    auto logs = parse_file(path);

    REQUIRE(logs.size() == 6);
    
    // Check first entry
    REQUIRE(logs[0].timestamp == "2026-10-01T08:00:00");
    REQUIRE(logs[0].severity == Severity::INFO);
    REQUIRE(logs[0].message == "Substation_A initialization complete.");
    
    // Check WARN entry (line 2)
    REQUIRE(logs[1].timestamp == "2026-10-01T08:05:12");
    REQUIRE(logs[1].severity == Severity::WARN);
    REQUIRE(logs[1].message == "Bus_12 voltage dropping (0.94 pu).");
    
    // Check CRITICAL entries
    REQUIRE(logs[2].severity == Severity::CRITICAL);
    REQUIRE(logs[4].severity == Severity::CRITICAL);
}

TEST_CASE("parse_file: handles missing file gracefully", "[log_parser]") {
    auto logs = parse_file("/nonexistent/path/file.txt");
    REQUIRE(logs.empty());
}

TEST_CASE("parse_file: handles empty lines", "[log_parser]") {
    // Create temp file with empty lines
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "test_log_empty.txt";
    {
        std::ofstream f(tmp);
        f << "2026-10-01T08:00:00 [INFO] First\n";
        f << "\n";
        f << "2026-10-01T08:01:00 [WARN] Second\n";
        f << "\n";
    }
    
    auto logs = parse_file(tmp.string());
    REQUIRE(logs.size() == 2);
    REQUIRE(logs[0].message == "First");
    REQUIRE(logs[1].message == "Second");
    
    std::filesystem::remove(tmp);
}

TEST_CASE("parse_file: handles malformed lines gracefully", "[log_parser]") {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "test_log_malformed.txt";
    {
        std::ofstream f(tmp);
        f << "2026-10-01T08:00:00 [INFO] Valid\n";
        f << "malformed line without brackets\n";
        f << "2026-10-01T08:01:00 [WARN] Also valid\n";
        f << "no timestamp [INFO] Missing timestamp\n";
    }
    
    auto logs = parse_file(tmp.string());
    // Parser accepts lines with [SEVERITY] pattern, so "no timestamp [INFO]..." is valid
    // Only the completely malformed line is skipped
    REQUIRE(logs.size() == 3);
    
    std::filesystem::remove(tmp);
}