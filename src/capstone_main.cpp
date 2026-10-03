#include <iostream>
#include <vector>
#include <string_view>
#include <string>
#include <format>
#include <ranges>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <concepts>
#include <span>

// ============================================================================
// PHASE 3: C++20 CONCEPTS
// Guarantee our solvers only run with high-precision floating point types.
// ============================================================================
template <typename T>
concept PrecisionFloat = std::floating_point<T>;

// ============================================================================
// PHASE 2: MEMORY MANAGEMENT & STRUCTS
// ============================================================================
struct Branch {
    std::string id;
    int from_bus;
    int to_bus;
    double reactance_pu;
    double capacity_mw;
};

struct ContingencyResult {
    std::string tripped_branch_id;
    double max_overload_pct;
    bool causes_islanding;
};

// ============================================================================
// PHASE 1: MOCK DATA INGESTION (Fast initialization)
// ============================================================================
std::vector<Branch> load_grid_data() {
    return {
        {"L1", 1, 2, 0.020, 500.0},
        {"L2", 2, 3, 0.015, 400.0},
        {"L3", 1, 3, 0.025, 300.0},
        {"L4", 3, 4, 0.010, 600.0}, // Critical backbone line
        {"L5", 4, 5, 0.030, 250.0}, // Radial line
        {"L6", 2, 5, 0.040, 200.0},
        {"L7", 5, 6, 0.020, 450.0}, // Critical backbone line
        {"L8", 1, 6, 0.050, 150.0}  // Radial line
    };
}

// ============================================================================
// PHASE 2 & 3: MEMORY VIEWS & CONSTRAINED MATH SOLVER
// Using std::span ensures we never accidentally copy the massive grid array.
// ============================================================================
template <PrecisionFloat T>
ContingencyResult simulate_n_minus_1(std::span<const Branch> full_grid, const Branch& tripped_branch) {
    // Simulate complex math (e.g., building Y-bus, solving DC Power Flow, checking thermal limits)
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); 
    
    T overload = 0.0;
    bool islanding = false;
    
    // Dummy logic to generate interesting results based on branch properties
    if (tripped_branch.capacity_mw > 400.0) {
        overload = 135.5; // Heavy backbone lines cause massive cascading overloads
    } else if (tripped_branch.id == "L5" || tripped_branch.id == "L8") {
        islanding = true; // Dropping radial lines isolates buses
    } else {
        overload = 85.0; // Safe (under 100% capacity)
    }
    
    return {tripped_branch.id, overload, islanding};
}

// ============================================================================
// PHASE 5: CONCURRENCY GLOBALS
// ============================================================================
std::mutex results_mutex;
std::vector<ContingencyResult> global_results;
std::atomic<int> processed_count{0};

// Worker Thread Function
void worker_thread(std::span<const Branch> full_grid, std::span<const Branch> work_chunk) {
    for (const auto& branch : work_chunk) {
        
        // Execute the simulation
        auto result = simulate_n_minus_1<double>(full_grid, branch);
        
        // Safely push to global vector using Scoped Lock & Move Semantics
        {
            std::scoped_lock lock(results_mutex);
            global_results.push_back(std::move(result)); 
        }
        
        processed_count.fetch_add(1, std::memory_order_relaxed);
    }
}

// ============================================================================
// MAIN EXECUTION
// ============================================================================
int main() {
    std::cout << "========================================================\n";
    std::cout << "  MODERN C++ CAPSTONE: Parallel N-1 Contingency Engine  \n";
    std::cout << "========================================================\n\n";

    auto grid = load_grid_data();
    std::cout << std::format("[System] Loaded {} branches into memory.\n", grid.size());

    // --- Phase 5: Thread Pool Execution ---
    std::cout << "[System] Spawning thread pool for N-1 analysis...\n";
    auto start_time = std::chrono::high_resolution_clock::now();
    
    {
        std::vector<std::jthread> pool;
        
        // Split work exactly in half for 2 threads using std::span
        size_t half = grid.size() / 2;
        std::span<const Branch> chunk1(grid.data(), half);
        std::span<const Branch> chunk2(grid.data() + half, grid.size() - half);
        std::span<const Branch> full_view(grid);
        
        pool.emplace_back(worker_thread, full_view, chunk1);
        pool.emplace_back(worker_thread, full_view, chunk2);
        
        // jthreads automatically join here, safely waiting for simulation to finish.
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << std::format("[System] Analysis complete. {} contingencies processed in {} ms.\n\n", 
                             processed_count.load(), duration.count());

    // --- Phase 4: Ranges & Data Pipelines ---
    std::cout << "--- TOP CRITICAL CONTINGENCIES (THERMAL OVERLOAD) ---\n";
    
    // Sort results descending by overload
    std::ranges::sort(global_results, [](const auto& a, const auto& b) {
        return a.max_overload_pct > b.max_overload_pct;
    });
    
    // Functional Pipeline: Filter out islanding, filter healthy lines, take top 3
    auto critical_views = global_results 
        | std::views::filter([](const ContingencyResult& r) { return !r.causes_islanding; })
        | std::views::filter([](const ContingencyResult& r) { return r.max_overload_pct > 100.0; })
        | std::views::take(3);

    for (const auto& res : critical_views) {
        std::cout << std::format(" -> Tripping {} causes cascaded OVERLOAD of {:.1f}%\n", 
                                 res.tripped_branch_id, res.max_overload_pct);
    }
    
    std::cout << "\n--- ISLANDING CONTINGENCIES (LOSS OF LOAD) ---\n";
    
    auto islanding_views = global_results
        | std::views::filter([](const ContingencyResult& r) { return r.causes_islanding; });
        
    for (const auto& res : islanding_views) {
        std::cout << std::format(" -> Tripping {} isolates buses, causing grid islanding.\n", 
                                 res.tripped_branch_id);
    }

    return 0;
}
