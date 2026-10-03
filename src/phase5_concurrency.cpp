#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <format>

// --- Synchronization: Mutex and Scoped Lock ---
// When multiple threads print to std::cout at the same time, the text gets garbled.
// We use a mutex to ensure only one thread prints at a time.
std::mutex log_mutex;

void safe_print(const std::string& msg) {
    // std::scoped_lock (C++17) is RAII for locks. 
    // It locks the mutex here, and AUTOMATICALLY unlocks it when the function ends.
    // If an exception occurs, it still unlocks safely. No deadlocks!
    std::scoped_lock lock(log_mutex);
    std::cout << msg << "\n";
}

// --- Atomics: Lock-Free Thread Safety ---
// We want to count total tasks completed. Using a mutex just to increment a number is very slow.
// std::atomic allows multiple threads to safely increment the number concurrently without locks.
std::atomic<int> total_contingencies_analyzed{0};

// --- C++20 Cooperative Cancellation (std::stop_token) ---
// This is the worker function. It receives a 'stop_token' from its controlling thread.
void worker_node(std::stop_token stoken, int node_id) {
    safe_print(std::format("Node {} online.", node_id));
    
    int local_work = 0;
    
    // The loop checks if the main thread has requested it to stop.
    // This allows the thread to finish its current calculation safely before exiting.
    while (!stoken.stop_requested()) {
        
        // Simulate a complex N-1 contingency calculation (takes 100ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Safely increment the global counter without a lock
        total_contingencies_analyzed.fetch_add(1, std::memory_order_relaxed);
        local_work++;
        
        if (local_work % 5 == 0) {
            safe_print(std::format("  -> Node {} completed {} calculations...", node_id, local_work));
        }
    }
    
    safe_print(std::format("Node {} gracefully shutting down.", node_id));
}

int main() {
    std::cout << "=== Phase 5: Modern Concurrency (C++20 std::jthread) ===\n\n";

    {
        // We use std::jthread (Joining Thread) instead of older std::thread
        std::vector<std::jthread> cluster;
        
        // Spawn 4 worker threads
        for (int i = 0; i < 4; ++i) {
            // std::jthread automatically generates a stop_token and passes it to the function!
            cluster.emplace_back(worker_node, i + 1);
        }
        
        safe_print("[Main] All nodes spawned. Letting them run for ~800ms...");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        
        safe_print("\n[Main] Requesting all nodes to stop...");
        
        // We gently ask all threads to stop (flips their stop_token to true)
        for (auto& node : cluster) {
            node.request_stop();
        }
        
        safe_print("[Main] End of scope reached. jthreads will automatically join!");
        // --- THE MAGIC OF jthread ---
        // Right here, as 'cluster' goes out of scope, the destructor for every std::jthread 
        // is called. It will AUTOMATICALLY wait (.join()) for the threads to finish cleanly.
        // Old std::thread would just crash the program if you forgot to call .join().
    } 

    std::cout << std::format("\n[Main] Final Report: {} total contingencies analyzed safely.\n", 
                             total_contingencies_analyzed.load());
    
    return 0;
}
