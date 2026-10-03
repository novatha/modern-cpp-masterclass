module;
#include <string_view>
#include <string>
export module grid_parser;

export enum class Severity { INFO, WARNING, CRITICAL, UNKNOWN };

export struct LogEntry {
    std::string_view timestamp;
    Severity severity;
    std::string_view message;
};

export LogEntry parse_log_line(std::string_view line) {
    size_t first_space = line.find(' ');
    size_t second_space = line.find(' ', first_space + 1);
    
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
        return {"", Severity::UNKNOWN, line};
    }

    std::string_view ts = line.substr(0, first_space);
    std::string_view sev_str = line.substr(first_space + 1, second_space - first_space - 1);
    std::string_view msg = line.substr(second_space + 1);

    Severity sev = Severity::UNKNOWN;
    if (sev_str == "[INFO]") sev = Severity::INFO;
    else if (sev_str == "[WARNING]") sev = Severity::WARNING;
    else if (sev_str == "[CRITICAL]") sev = Severity::CRITICAL;

    return {ts, sev, msg};
}
