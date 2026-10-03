#pragma once

#include <string_view>
#include <string>
#include <vector>

namespace grid_logs {

    // Enum class for strong typing
    enum class Severity {
        INFO,
        WARN,
        CRITICAL,
        UNKNOWN
    };

    // Struct to hold parsed log entries
    struct LogEntry {
        std::string timestamp;
        Severity severity;
        std::string message;
    };

    // Helper to map string view to severity
    constexpr Severity parse_severity(std::string_view sev_str) {
        if (sev_str == "INFO") return Severity::INFO;
        if (sev_str == "WARN") return Severity::WARN;
        if (sev_str == "CRITICAL") return Severity::CRITICAL;
        return Severity::UNKNOWN;
    }

    // Main parsing function
    std::vector<LogEntry> parse_file(std::string_view filepath);

} // namespace grid_logs
