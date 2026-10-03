#include <iostream>
#include <vector>
#include <chrono>
#include <expected>
#include <string>
#include <numeric>

// ============================================================================
// TOPIC 1: C++23 Modern Error Handling (std::expected)
// Exceptions (try/catch) are extremely slow when thrown. 
// std::expected forces the caller to handle errors without performance penalties.
// ============================================================================
enum class SystemError {
    InvalidTelemetry,
    ConvergenceFailed,
    MatrixSingular
};

std::string error_to_string(SystemError err) {
    switch(err) {
        case SystemError::InvalidTelemetry: return "Invalid Telemetry Data";
        case SystemError::ConvergenceFailed: return "Newton-Raphson Convergence Failed";
        case SystemError::MatrixSingular: return "Y-Bus Matrix is Singular (Islanding)";
        default: return "Unknown";
    }
}

// Returns a double on success, or a SystemError on failure
std::expected<double, SystemError> solve_power_flow(double load_mw) {
    if (load_mw < 0) {
        return std::unexpected(SystemError::InvalidTelemetry);
    }
    if (load_mw > 10000) {
        return std::unexpected(SystemError::ConvergenceFailed);
    }
    return load_mw * 0.95; // Mock successful calculation
}

// ============================================================================
// TOPIC 2: Hardware Sympathy (CPU Cache & Data-Oriented Design)
// Object-Oriented Programming (OOP) creates memory layouts that CPU caches hate.
// ============================================================================

const int NUM_BRANCHES = 10'000'000; // 10 million branches

// 1. Array of Structs (AoS) - The OOP Way
// Each branch is a localized object in memory.
struct BranchOOP {
    int id;
    double r;
    double x;      // We want to calculate the sum of all reactances
    double limit;
};

// 2. Struct of Arrays (SoA) - The High-Performance Way
// Data is packed tightly by type.
struct GridSOA {
    std::vector<int> id;
    std::vector<double> r;
    std::vector<double> x;
    std::vector<double> limit;

    GridSOA(int size) : id(size, 1), r(size, 0.01), x(size, 0.05), limit(size, 500.0) {}
};

int main() {
    std::cout << "=== Phase 6: Advanced Hardware Sympathy & Error Handling ===\n\n";

    // --- Demo 1: std::expected ---
    std::cout << "--- 1. C++23 std::expected ---\n";
    std::vector<double> test_loads = {500.0, -10.0, 15000.0};
    
    for (double load : test_loads) {
        auto result = solve_power_flow(load);
        if (result.has_value()) {
            std::cout << "[SUCCESS] Flow solved: " << result.value() << " MW\n";
        } else {
            // No try/catch needed! 
            std::cout << "[ERROR] Failed to solve for " << load << " MW. Reason: " 
                      << error_to_string(result.error()) << "\n";
        }
    }

    // --- Demo 2: Hardware Sympathy (Cache Lines) ---
    std::cout << "\n--- 2. Hardware Sympathy Benchmark (10 Million Branches) ---\n";
    
    std::cout << "Allocating memory...\n";
    std::vector<BranchOOP> aos_grid(NUM_BRANCHES, {1, 0.01, 0.05, 500.0});
    GridSOA soa_grid(NUM_BRANCHES);

    double total_reactance = 0.0;

    // Benchmark AoS (OOP)
    auto start_aos = std::chrono::high_resolution_clock::now();
    for (const auto& branch : aos_grid) {
        total_reactance += branch.x; // Cache miss heavy! CPU loads id, r, x, limit into cache, but only uses x.
    }
    auto end_aos = std::chrono::high_resolution_clock::now();
    auto aos_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_aos - start_aos).count();

    // Benchmark SoA (Data-Oriented)
    total_reactance = 0.0;
    auto start_soa = std::chrono::high_resolution_clock::now();
    for (const auto& x_val : soa_grid.x) {
        total_reactance += x_val; // Cache perfect! CPU loads only reactances tightly packed in memory.
    }
    auto end_soa = std::chrono::high_resolution_clock::now();
    auto soa_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_soa - start_soa).count();

    std::cout << "OOP (Array of Structs) Time:  " << aos_ms << " ms\n";
    std::cout << "Data-Oriented (SoA) Time:     " << soa_ms << " ms\n";
    std::cout << "Speedup: Data-Oriented is ~" << (double)aos_ms / (soa_ms == 0 ? 1 : soa_ms) << "x faster due to CPU Cache efficiency!\n";

    return 0;
}
