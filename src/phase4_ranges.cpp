#include <iostream>
#include <vector>
#include <ranges>
#include <format>
#include <optional>
#include <string>

// A struct representing a Phasor Measurement Unit (PMU) reading
struct SensorReading {
    int time_ms;
    double voltage_pu;
    bool is_valid; // false if sensor glitched or packet was lost
};

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
       THE PIPELINE:
       In old C++, you'd write a massive messy `for` loop with nested `if` statements,
       or use `std::copy_if` which allocates intermediate vectors (wasting RAM).
       
       With C++20, we use the pipe operator `|` to chain "views".
       Views are LAZY. They don't copy data. They just describe the operations.
    */
    // SNIPPET: phase4-ranges-pipeline
    auto critical_drops = readings 
        | std::views::filter([](const SensorReading& r) { return r.is_valid; })              // Step 1: Ignore glitches
        | std::views::filter([](const SensorReading& r) { return r.voltage_pu < 0.95; })     // Step 2: Find low voltage
        | std::views::transform([](const SensorReading& r) {                                 // Step 3: Map to a string
            return std::format("Time {}ms: CRITICAL VOLTAGE {:.2f} pu", r.time_ms, r.voltage_pu);
        })
        | std::views::take(2);                                                               // Step 4: Stop after finding 2

    // The data is actually processed right here, one element at a time:
    for (const std::string& alert : critical_drops) {
        std::cout << "  -> " << alert << "\n";
    }
    // END SNIPPET


    std::cout << "\n--- 2. Modern Error Handling (std::optional) ---\n";
    
    // In old C++, if a search failed, you returned `-1` or `nullptr`.
    // In modern C++, you return `std::optional` to explicitly say "This might fail".
    // SNIPPET: phase4-optional-find
    auto find_first_drop = [](const std::vector<SensorReading>& data) -> std::optional<int> {
        for (const auto& r : data) {
            if (r.is_valid && r.voltage_pu < 0.90) {
                return r.time_ms; // Found it!
            }
        }
        return std::nullopt; // No data matched
    };

    // The "if with initializer" syntax (C++17) combined with optional
    if (auto drop_time = find_first_drop(readings); drop_time.has_value()) {
        std::cout << "  -> Extreme drop (<0.90) detected at " << drop_time.value() << "ms\n";
    } else {
        std::cout << "  -> No extreme drops detected.\n";
    }
    // END SNIPPET

    return 0;
}
