#include <iostream>
#include <vector>
#include <format>
#include "phase4_pipeline.hpp"

using namespace grid_telemetry;

int main() {
    std::cout << "=== Phase 4: Data Pipelines with C++20 Ranges ===\n\n";

    // Simulated timeseries data from a power bus
    std::vector<SensorReading> readings = {
        {0,    1.00, true},
        {100,  0.99, true},
        {200, -99.9, false}, // Sensor glitch (invalid)
        {300,  0.97, true},
        {400,  0.92, true},  // Voltage drop!
        {500,  0.00, false}, // Network packet lost
        {600,  0.88, true},  // Critical drop!
        {700,  0.85, true}   // Another drop (but we only want the first 2)
    };

    std::cout << "--- 1. C++20 Lazy Ranges Pipeline ---\n";

    /*
       THE PIPELINE (in phase4_pipeline.hpp):
       In old C++, you'd write a massive messy `for` loop with nested `if`
       statements, or use `std::copy_if` which allocates intermediate vectors
       (wasting RAM).

       With C++20, we use the pipe operator `|` to chain "views".
       Views are LAZY. They don't copy data. They just describe the operations.
    */
    for (const std::string& alert : critical_drop_alerts(readings)) {
        std::cout << "  -> " << alert << "\n";
    }

    std::cout << "\n--- 2. Modern Error Handling (std::optional) ---\n";

    // The "if with initializer" syntax (C++17) combined with optional
    if (auto drop_time = find_first_drop(readings); drop_time.has_value()) {
        std::cout << "  -> Extreme drop (<0.90) detected at " << drop_time.value() << "ms\n";
    } else {
        std::cout << "  -> No extreme drops detected.\n";
    }

    return 0;
}
