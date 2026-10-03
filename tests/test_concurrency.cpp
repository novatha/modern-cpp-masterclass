#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <numeric>

TEST_CASE("std::jthread: basic join on scope exit", "[concurrency]") {
    bool joined = false;
    {
        std::jthread t([&joined](std::stop_token) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            joined = true;
        });
    } // jthread joins here
    REQUIRE(joined);
}

TEST_CASE("std::jthread: stop_token stops thread", "[concurrency]") {
    std::atomic<int> counter{0};
    std::jthread t([&counter](std::stop_token st) {
        while (!st.stop_requested()) {
            counter.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t.request_stop();
    // t joins automatically
    
    int final_count = counter.load();
    REQUIRE(final_count > 0); // Did some work
}

TEST_CASE("std::scoped_lock: prevents deadlock with multiple mutexes", "[concurrency]") {
    std::mutex m1, m2;
    int shared1 = 0, shared2 = 0;
    
    auto worker = [&](int id) {
        std::scoped_lock lock(m1, m2);
        shared1 += id;
        shared2 += id * 2;
    };
    
    // Use scope to ensure threads join before REQUIRE
    {
        std::vector<std::jthread> threads;
        for (int i = 1; i <= 4; ++i) {
            threads.emplace_back(worker, i);
        }
    } // threads join here
    
    REQUIRE(shared1 == 10); // 1+2+3+4
    REQUIRE(shared2 == 20); // 2+4+6+8
}

TEST_CASE("std::atomic: fetch_add is thread-safe", "[concurrency]") {
    std::atomic<int> counter{0};
    constexpr int threads = 4;
    constexpr int iterations = 10000;
    
    {
        std::vector<std::jthread> workers;
        for (int i = 0; i < threads; ++i) {
            workers.emplace_back([&counter](std::stop_token) {
                for (int j = 0; j < iterations; ++j) {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    } // workers join here
    
    REQUIRE(counter.load() == threads * iterations);
}

TEST_CASE("std::atomic: memory_order_relaxed is sufficient for counters", "[concurrency]") {
    std::atomic<int> counter{0};
    
    {
        std::vector<std::jthread> workers;
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&counter](std::stop_token) {
                for (int j = 0; j < 1000; ++j) {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    } // workers join here
    
    REQUIRE(counter.load() == 8000);
}

TEST_CASE("Concurrency: no data race on shared vector with mutex", "[concurrency][race]") {
    std::mutex vec_mutex;
    std::vector<int> shared_vec;
    constexpr int num_threads = 8;
    constexpr int pushes_per_thread = 100;
    
    // Use std::thread with explicit join for deterministic test
    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&](int tid) {
            for (int i = 0; i < pushes_per_thread; ++i) {
                std::scoped_lock lock(vec_mutex);
                shared_vec.push_back(tid * 1000 + i);
            }
        }, t);
    }
    for (auto& th : workers) {
        th.join();
    }
    
    REQUIRE(shared_vec.size() == num_threads * pushes_per_thread);
    // Verify all values present
    std::sort(shared_vec.begin(), shared_vec.end());
    for (int t = 0; t < num_threads; ++t) {
        for (int i = 0; i < pushes_per_thread; ++i) {
            int expected = t * 1000 + i;
            REQUIRE(std::binary_search(shared_vec.begin(), shared_vec.end(), expected));
        }
    }
}

TEST_CASE("std::jthread: exception safety - thread function should not throw", "[concurrency]") {
    // jthread behavior with exceptions is implementation-defined in some cases
    // Best practice: thread functions should not throw; handle errors internally
    std::jthread t([](std::stop_token) {
        // Simulate error handling without throwing
        int result = 0;
        try {
            throw std::runtime_error("internal error");
        } catch (...) {
            result = -1; // Handle internally
        }
        // Thread completes normally
    });
    // Should not crash
    SUCCEED();
}