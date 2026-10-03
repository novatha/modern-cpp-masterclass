#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <tuple>
#include <format>

// 1. Compile-time constant evaluation (constexpr vs consteval)
// consteval guarantees this MUST be evaluated at compile time
// SNIPPET: phase1-consteval-fib
consteval int compile_time_fibonacci(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int a = 0;
    int b = 1;
    for (int i = 2; i <= n; ++i) {
        int next = a + b;
        a = b;
        b = next;
    }
    return b;
}
// END SNIPPET

// 2. Non-owning string slices with std::string_view
// Avoids costly heap allocations when inspecting substrings
// SNIPPET: phase1-string-view
void print_slice_info(std::string_view text) {
    std::cout << std::format("Slice length: {}, prefix(4): \"{}\"\n", 
                             text.size(), 
                             text.substr(0, std::min<size_t>(4, text.size())));
}
// END SNIPPET

// 3. Simple struct for structured bindings
struct Measurement {
    std::string sensor_name;
    double voltage;
    double current;
};

int main() {
    std::cout << "========================================\n";
    std::cout << "  Modern C++ (C++20) Phase 1: Showcase  \n";
    std::cout << "========================================\n\n";

    // --- A. Compile-time execution (consteval) ---
    constexpr int fib10 = compile_time_fibonacci(10);
    std::cout << std::format("[1] consteval: Fibonacci(10) = {}\n", fib10);

    // --- B. Structured Bindings (unpacking objects cleanly) ---
    // SNIPPET: phase1-structured-binding
    Measurement m{"Bus-01_Sensor", 230.5, 12.8};
    auto [name, v, i] = m; // unpacks struct fields cleanly
    double power = v * i;
    std::cout << std::format("[2] Structured Binding:\n"
                             "    Sensor: {}\n"
                             "    Voltage: {:.1f} V, Current: {:.1f} A -> Apparent Power: {:.2f} VA\n",
                             name, v, i, power);

    // Structured binding with std::tuple / std::pair
    auto get_coordinates = []() -> std::pair<double, double> {
        return {-0.7893, 100.3542}; // e.g., Padang / Sumatra latitude & longitude
    };
    auto [lat, lon] = get_coordinates();
    std::cout << std::format("    Coordinates: ({:.4f}, {:.4f})\n", lat, lon);
    // END SNIPPET

    // --- C. std::string_view (Zero-copy string inspection) ---
    std::cout << "\n[3] std::string_view (No allocations):\n";
    // SNIPPET: phase1-string-view-usage
    std::string full_log = "2026-10-01T11:45:00 [CRITICAL] Transformer Overload Detected";
    // Passing substring view without copying or allocating new string
    std::string_view log_view = full_log;
    print_slice_info(log_view);
    // END SNIPPET

    // --- D. Range-based for loop with init statement (C++20) ---
    std::cout << "\n[4] Range-for with Init Statement:\n";
    // Initializer localizes 'weights' to the loop scope only
    // SNIPPET: phase1-range-for-init
    for (std::vector<double> readings{1.02, 0.99, 1.05, 0.96}; 
         const auto& val : readings) {
        std::cout << std::format("    Voltage pu: {:.2f} ({})\n", 
                                 val, (val >= 0.95 && val <= 1.05) ? "NORMAL" : "VIOLATION");
    }
    // END SNIPPET

    return 0;
}
