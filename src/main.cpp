#include "log_parser.hpp"
#include <iostream>
#include <format>
#include <vector>
#include <string_view>

std::string_view severity_to_string(grid_logs::Severity s) {
    using enum grid_logs::Severity; // C++20 feature
    switch (s) {
        case INFO: return "\033[32mINFO\033[0m";       // Green
        case WARN: return "\033[33mWARN\033[0m";       // Yellow
        case CRITICAL: return "\033[31mCRITICAL\033[0m"; // Red
        default: return "UNKNOWN";
    }
}

int main() {
    std::cout << "--- Power Grid Log Analyzer ---\n\n";

    std::string log_path = std::string(DATA_DIR) + "/sample_grid_logs.txt";
    std::vector<grid_logs::LogEntry> logs = grid_logs::parse_file(log_path);

    int critical_count = 0;

    for (const auto& [timestamp, severity, message] : logs) { // Structured Binding
        if (severity == grid_logs::Severity::CRITICAL) {
            critical_count++;
        }
        
        // Using C++20 std::format for clean output
        std::cout << std::format("[{}] [{}] {}\n", 
                                 timestamp, 
                                 severity_to_string(severity), 
                                 message);
    }

    std::cout << "\n-----------------------------------------------\n";
    std::cout << std::format("Total Events: {}\n", logs.size());
    std::cout << std::format("Critical Alerts: {}\n", critical_count);

    return 0;
}
