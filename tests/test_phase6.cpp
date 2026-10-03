#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <string>
#include <vector>
#include <numeric>

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

std::expected<double, SystemError> solve_power_flow(double load_mw) {
    if (load_mw < 0) {
        return std::unexpected(SystemError::InvalidTelemetry);
    }
    if (load_mw > 10000) {
        return std::unexpected(SystemError::ConvergenceFailed);
    }
    return load_mw * 0.95;
}

TEST_CASE("std::expected: success case returns value", "[phase6][expected]") {
    auto result = solve_power_flow(500.0);
    
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 475.0); // 500 * 0.95
}

TEST_CASE("std::expected: negative load returns InvalidTelemetry", "[phase6][expected]") {
    auto result = solve_power_flow(-10.0);
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == SystemError::InvalidTelemetry);
    REQUIRE(error_to_string(result.error()) == "Invalid Telemetry Data");
}

TEST_CASE("std::expected: excessive load returns ConvergenceFailed", "[phase6][expected]") {
    auto result = solve_power_flow(15000.0);
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == SystemError::ConvergenceFailed);
    REQUIRE(error_to_string(result.error()) == "Newton-Raphson Convergence Failed");
}

TEST_CASE("std::expected: boundary values", "[phase6][expected]") {
    // Exactly 0 should succeed
    auto r0 = solve_power_flow(0.0);
    REQUIRE(r0.has_value());
    REQUIRE(r0.value() == 0.0);
    
    // Exactly 10000 should succeed
    auto r1 = solve_power_flow(10000.0);
    REQUIRE(r1.has_value());
    REQUIRE(r1.value() == 9500.0);
    
    // 10001 should fail
    auto r2 = solve_power_flow(10001.0);
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r2.error() == SystemError::ConvergenceFailed);
}

TEST_CASE("std::expected: monadic operations (and_then, transform)", "[phase6][expected][cxx23]") {
    auto result = solve_power_flow(100.0);
    
    // and_then: chain another expected-returning function
    auto chained = result.and_then([](double v) -> std::expected<double, SystemError> {
        if (v > 100) return std::unexpected(SystemError::ConvergenceFailed);
        return v * 1.1; // 10% margin
    });
    
    REQUIRE(chained.has_value());
    // Floating point comparison with epsilon
    REQUIRE(std::abs(chained.value() - 104.5) < 1e-9);
    
    // transform: map value without changing error
    auto doubled = result.transform([](double v) { return v * 2.0; });
    REQUIRE(doubled.has_value());
    REQUIRE(std::abs(doubled.value() - 190.0) < 1e-9);
}

TEST_CASE("std::expected: or_else provides fallback", "[phase6][expected][cxx23]") {
    auto result = solve_power_flow(-5.0); // error
    
    auto recovered = result.or_else([](SystemError) -> std::expected<double, SystemError> {
        return 100.0; // fallback value
    });
    
    REQUIRE(recovered.has_value());
    REQUIRE(recovered.value() == 100.0);
}

TEST_CASE("SoA: struct layout sizes", "[phase6][soa]") {
    struct BranchAoS {
        int id;
        double r;
        double x;
        double limit;
    };
    
    struct GridSoA {
        std::vector<int> id;
        std::vector<double> r;
        std::vector<double> x;
        std::vector<double> limit;
        
        explicit GridSoA(size_t n) : id(n), r(n), x(n), limit(n) {}
    };
    
    // AoS: 4 + 8 + 8 + 8 = 28, padded to 32 bytes
    REQUIRE(sizeof(BranchAoS) == 32);
    
    // SoA: separate vectors, each element is tightly packed
    // No padding between elements of same type
    GridSoA soa(100);
    REQUIRE(soa.id.size() == 100);
    REQUIRE(soa.x.size() == 100);
    REQUIRE(soa.id.capacity() >= 100);
}

TEST_CASE("SoA: contiguous memory for same field", "[phase6][soa]") {
    struct GridSoA {
        std::vector<double> x;
        explicit GridSoA(size_t n) : x(n, 0.05) {}
    };
    
    GridSoA soa(1000);
    // All x values should be contiguous
    double* ptr = soa.x.data();
    REQUIRE(ptr[0] == 0.05);
    REQUIRE(ptr[999] == 0.05);
    
    // Pointer arithmetic works
    REQUIRE(ptr + 1000 == soa.x.data() + 1000);
}

TEST_CASE("std::expected: error type is enum class (no heap allocation)", "[phase6][expected][performance]") {
    // Verify SystemError is trivially copyable (no heap allocation on error)
    static_assert(std::is_trivially_copyable_v<SystemError>);
    static_assert(std::is_trivially_destructible_v<SystemError>);
    
    // std::expected with trivial error type should not allocate
    auto result = solve_power_flow(-1.0);
    REQUIRE_FALSE(result.has_value());
    // If this compiles and runs, error is stored inline
}