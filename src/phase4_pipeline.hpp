#pragma once

#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace grid_telemetry {

// A struct representing a Phasor Measurement Unit (PMU) reading
struct SensorReading {
    int time_ms;
    double voltage_pu;
    bool is_valid; // false if sensor glitched or packet was lost
};

// Build a lazy ranges pipeline:
//   1. Ignore invalid/glitched readings
//   2. Keep only low-voltage readings (< 0.95 pu)
//   3. Map each reading to a human-readable alert string
//   4. Stop after `max_alerts` hits
//
// The views are LAZY: no intermediate vectors are allocated while
// chaining. The final alerts are materialized into a vector only once.
// SNIPPET: phase4-ranges-pipeline
inline std::vector<std::string>
critical_drop_alerts(const std::vector<SensorReading>& readings,
                     std::size_t max_alerts = 2) {
    auto alerts = readings
        | std::views::filter([](const SensorReading& r) { return r.is_valid; })  // Step 1: Ignore glitches
        | std::views::filter([](const SensorReading& r) { return r.voltage_pu < 0.95; })  // Step 2: Low voltage
        | std::views::transform([](const SensorReading& r) {  // Step 3: Map to a string
              return std::format("Time {}ms: CRITICAL VOLTAGE {:.2f} pu",
                                 r.time_ms, r.voltage_pu);
          })
        | std::views::take(max_alerts);  // Step 4: Stop after max_alerts

    // Consume the lazy view once into a concrete vector.
    // (take_view's counted_iterator cannot feed std::vector's
    //  iterator-pair constructor directly in C++20.)
    std::vector<std::string> result;
    for (const auto& alert : alerts) {
        result.push_back(alert);
    }
    return result;
}
// END SNIPPET

// In old C++, if a search failed, you returned `-1` or `nullptr`.
// In modern C++, you return `std::optional` to explicitly say "This might fail".
// SNIPPET: phase4-optional-find
inline std::optional<int> find_first_drop(const std::vector<SensorReading>& data) {
    for (const auto& r : data) {
        if (r.is_valid && r.voltage_pu < 0.90) {
            return r.time_ms; // Found it!
        }
    }
    return std::nullopt; // No data matched
}
// END SNIPPET

} // namespace grid_telemetry
