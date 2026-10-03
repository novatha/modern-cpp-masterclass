#include <iostream>
#include <vector>
#include <chrono>
#include <expected>
#include <string>
#include <numeric>
#include <random>
#include <algorithm>

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

    GridSOA(int size) : id(size), r(size), x(size), limit(size) {
        std::iota(id.begin(), id.end(), 1);
        std::fill(r.begin(), r.end(), 0.01);
        std::fill(x.begin(), x.end(), 0.05);
        std::fill(limit.begin(), limit.end(), 500.0);
    }
};

// Prevent dead-store elimination: force compiler to keep the computation
inline void consume_result(double& val) {
    asm volatile("" : "+r"(val) : : "memory");
}

// Strided access: simulate random/non-sequential access pattern
// where cache locality matters most
std::vector<size_t> generate_strided_indices(size_t n, size_t stride) {
    std::vector<size_t> indices;
    indices.reserve(n);
    for (size_t i = 0; i < n; i += stride) {
        indices.push_back(i);
    }
    return indices;
}

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
            std::cout << "[ERROR] Failed to solve for " << load << " MW. Reason: " 
                      << error_to_string(result.error()) << "\n";
        }
    }

    // --- Demo 2: Hardware Sympathy (Cache Lines) ---
    std::cout << "\n--- 2. Hardware Sympathy Benchmark (10 Million Branches) ---\n";
    
    std::cout << "Allocating memory...\n";
    std::vector<BranchOOP> aos_grid(NUM_BRANCHES, {1, 0.01, 0.05, 500.0});
    GridSOA soa_grid(NUM_BRANCHES);

    // Use strided access to demonstrate cache effect (not streaming)
    const size_t stride = 64; // L1 cache line / sizeof(BranchOOP) ~ 64/32 = 2
    auto strided_indices = generate_strided_indices(NUM_BRANCHES, stride);
    
    double aos_sum = 0.0;
    double soa_sum = 0.0;

    // Benchmark AoS (OOP) - strided access
    auto start_aos = std::chrono::steady_clock::now();
    for (size_t idx : strided_indices) {
        aos_sum += aos_grid[idx].x;
    }
    auto end_aos = std::chrono::steady_clock::now();
    auto aos_us = std::chrono::duration_cast<std::chrono::microseconds>(end_aos - start_aos).count();

    // Benchmark SoA (Data-Oriented) - strided access
    auto start_soa = std::chrono::steady_clock::now();
    for (size_t idx : strided_indices) {
        soa_sum += soa_grid.x[idx];
    }
    auto end_soa = std::chrono::steady_clock::now();
    auto soa_us = std::chrono::duration_cast<std::chrono::microseconds>(end_soa - start_soa).count();

    // Anti-DSE: consume results
    consume_result(aos_sum);
    consume_result(soa_sum);

    std::cout << "OOP (Array of Structs) Time:  " << aos_us << " µs\n";
    std::cout << "Data-Oriented (SoA) Time:     " << soa_us << " µs\n";

    if (soa_us > 0 && aos_us > 0) {
        double speedup = static_cast<double>(aos_us) / soa_us;
        if (speedup > 1.05) {
            std::cout << "Result: SoA is " << speedup << "x faster (cache locality win)\n";
        } else if (speedup < 0.95) {
            std::cout << "Result: AoS is " << (1.0 / speedup) << "x faster (unexpected)\n";
        } else {
            std::cout << "Result: No significant difference (~1.0x)\n";
        }
    } else {
        std::cout << "Result: Timing too coarse, increase problem size\n";
    }

    return 0;
}
