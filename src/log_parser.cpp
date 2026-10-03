#include "log_parser.hpp"
#include <fstream>
#include <iostream>

namespace grid_logs {

    std::vector<LogEntry> parse_file(std::string_view filepath) {
        std::vector<LogEntry> entries;
        
        // Convert string_view to string for fstream constructor (legacy C++ interop)
        std::ifstream file{std::string(filepath)};
        if (!file.is_open()) {
            std::cerr << "Failed to open: " << filepath << "\n";
            return entries;
        }

        std::string line;
        while (std::getline(file, line)) {
            // Very simple parser based on assumed format: "TIMESTAMP [SEVERITY] MESSAGE"
            std::string_view line_view = line;

            auto space_idx1 = line_view.find(' ');
            if (space_idx1 == std::string_view::npos) continue;
            
            auto bracket_open = line_view.find('[', space_idx1);
            auto bracket_close = line_view.find(']', bracket_open);
            
            if (bracket_open == std::string_view::npos || bracket_close == std::string_view::npos) continue;

            // Extract using string_view (zero allocation)
            std::string_view ts_view = line_view.substr(0, space_idx1);
            std::string_view sev_view = line_view.substr(bracket_open + 1, bracket_close - bracket_open - 1);
            std::string_view msg_view = line_view.substr(bracket_close + 2); // Skip "] "

            // Store (allocate here)
            entries.push_back({
                std::string(ts_view),
                parse_severity(sev_view),
                std::string(msg_view)
            });
        }
        return entries;
    }

} // namespace grid_logs
