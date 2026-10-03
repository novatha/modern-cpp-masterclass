module;
#include <string_view>
#include <string>
export module grid_parser;

export enum class Severity { INFO, WARN, CRITICAL, UNKNOWN };

export struct LogEntry {
    std::string_view timestamp;
    Severity severity;
    std::string_view message;
};

export LogEntry parse_log_line(std::string_view line) {
    size_t first_space = line.find(' ');
    size_t bracket_open = line.find('[', first_space);
    size_t bracket_close = line.find(']', bracket_open);
    
    if (first_space == std::string_view::npos || bracket_open == std::string_view::npos || bracket_close == std::string_view::npos) {
        return {"", Severity::UNKNOWN, line};
    }

    std::string_view ts = line.substr(0, first_space);
    std::string_view sev_str = line.substr(bracket_open + 1, bracket_close - bracket_open - 1);
    std::string_view msg = line.substr(bracket_close + 2);

    Severity sev = Severity::UNKNOWN;
    if (sev_str == "INFO") sev = Severity::INFO;
    else if (sev_str == "WARN") sev = Severity::WARN;
    else if (sev_str == "CRITICAL") sev = Severity::CRITICAL;

    return {ts, sev, msg};
}
