#include <iostream>
#include <format>
#include <string_view>
import grid_parser;

int main() {
    std::cout << "=== Phase 1: Re-done with C++20 Modules (LLVM 23) ===\n\n";
    
    std::string_view raw_log = "2026-10-01T12:00:00Z [CRITICAL] Substation_Alpha Voltage Drop";
    auto entry = parse_log_line(raw_log);
    
    std::cout << std::format("Timestamp: {}\n", entry.timestamp);
    std::cout << std::format("Message: {}\n", entry.message);
    
    return 0;
}
