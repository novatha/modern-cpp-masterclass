# Modern C++ Mastery Roadmap (C++20 / C++23)

##  Target Goal
Transition from foundational programming into idiomatic **Modern C++** (focusing on C++20 and practical C++23 features), prioritizing:
- **Zero-overhead abstractions & RAII** (No naked `new`/`delete`, leak-free resource management)
- **Value semantics & Move semantics** (Fast copy elision, efficient data pipelines)
- **Expressive type safety** (Concepts, `std::string_view`, `std::span`, `std::optional`, `std::expected`)
- **Modern standard library** (`<ranges>`, `<format>`, `<chrono>`, smart pointers, algorithms)
- **Industry tooling** (CMake, Apple Clang, Sanitizers, unit testing with Catch2 / GoogleTest)

---

##  Phase 1: Environment, Foundations & Modules (Weeks 1-2)
> **Goal:** Set up a clean build workflow, embrace C++20 Modules, and unlearn legacy anti-patterns.

1. **Modern Toolchain & Build System**
   - Compiler flags: `-std=c++20 -Wall -Wextra -Wpedantic`
   - Modern CMake: targets, sanitizers (ASan/UBSan).
   - **C++20 Modules:** Replacing `#include` with `import`; writing `.cppm` module interfaces to speed up compilation and encapsulate code.

2. **Modern C++ Basics vs. Legacy C++**
   - Strong typing & auto: `auto`, structured binding (`auto [x, y] = tuple;`).
   - Range-based for loops with init statements: `for (auto& item : container)`.
   - Compile-time expressions: `constexpr` vs. `consteval`.
   - String handling without C-strings: `std::string` vs non-owning `std::string_view`.
   - Modern formatted output: `std::format` / `std::print` (C++20/23).

-  **Mini Project 1:** *Command-line Log Parser & Formatter*
  - Build a log parser using C++20 Modules, parsing timestamps with string views, and printing formatted summaries without dynamic raw allocations.

---

##  Phase 2: Memory Management, Dependencies & Testing (Weeks 3-4)
> **Goal:** Master resource cleanup, dependency management, and automated testing.

1. **Dependency Management & Testing**
   - Integrating **vcpkg** (or Conan) with CMake to pull third-party libraries.
   - Unit testing with **Catch2** or GoogleTest.

2. **RAII (Resource Acquisition Is Initialization)**
   - Custom wrapper classes managing OS handles or file descriptors.
   - Rule of Zero, Rule of Three, and Rule of Five.

3. **Smart Pointers & Ownership**
   - Exclusive ownership: `std::unique_ptr` and `std::make_unique`.
   - Shared & weak ownership: `std::shared_ptr`, `std::weak_ptr`.
   - Non-owning contiguous memory access: `std::span`.

4. **Move Semantics & Rvalue References**
   - Move constructor and move assignment operator.
   - `std::move` and `std::forward` (avoiding redundant copies).

-  **Mini Project 2:** *Custom Matrix / Tensor Storage Buffer*
  - Build a lightweight 2D array buffer featuring RAII, move semantics, and `std::span`.
  - **Requirement:** Fetch the `fmt` or `Catch2` library via `vcpkg` and write a full unit test suite verifying memory safety (no leaks under ASan).

---

##  Phase 3: Generic Programming & C++20 Concepts (Weeks 5-6)
> **Goal:** Write expressive, high-performance generic code with compiler-verified constraints.

1. **Templates & Modern Type Traits**
   - Function templates & Class templates.
   - Variadic templates & fold expressions (`(args + ...)`).
   - Compile-time branching: `if constexpr`.

2. **C++20 Concepts & Constraints**
   - Replacing cryptic SFINAE errors with readable `requires` clauses.
   - Standard concepts: `std::integral`, `std::floating_point`, `std::invocable`, `std::ranges::range`.
   - Defining custom concepts (e.g., `Numeric`, `Serializable`).

-  **Mini Project 3:** *Generic Event Dispatcher / Signal-Slot System*
  - Implement a type-safe observer / signal-slot event dispatcher constrained by concepts, supporting arbitrary callable objects (lambdas, function pointers, member functions).

---

##  Phase 4: Modern STL, Ranges & Functional Pipelines (Weeks 7-8)
> **Goal:** Write declarative, loop-free, high-efficiency data processing pipelines.

1. **Standard Containers & Vocabulary Types**
   - Sequence: `std::vector`, `std::array`, `std::deque`.
   - Associative / Unordered: `std::unordered_map` (flat maps in C++23).
   - Modern Vocabulary: `std::optional`, `std::variant`, `std::any`, `std::expected` (C++23 error handling without exceptions).

2. **Standard Algorithms & C++20 Ranges**
   - `<algorithm>`: `std::ranges::sort`, `std::ranges::find_if`, `std::ranges::transform`.
   - Range views & lazy composition: `std::views::filter`, `std::views::transform`, `std::views::take`.
   - Piping syntax: `auto result = data | std::views::filter(...) | std::views::transform(...);`

-  **Mini Project 4:** *Data Processing Pipeline (CSV/Timeseries Engine)*
  - Build an in-memory timeseries aggregator using `std::views` pipelines, computing moving averages, filtering outliers, and outputting formatted statistics with zero intermediate vector allocations.

---

##  Phase 5: Concurrency, Multithreading & Performance (Weeks 9-10)
> **Goal:** Modern multi-threaded programming without data races.

1. **C++ Threading Model**
   - C++20 cooperative cancellation & joining: `std::jthread` and `std::stop_token`.
   - Synchronization: `std::mutex`, `std::unique_lock`, `std::shared_mutex`, `std::scoped_lock` (deadlock avoidance).
   - Condition variables and C++20 counting semaphores/barriers (`std::counting_semaphore`, `std::latch`).

2. **Atomics & Lock-free Basics**
   - `std::atomic<T>`, memory orders (`std::memory_order_relaxed`, `acquire/release`).
   - Async tasks & futures: `std::async`, `std::future`, `std::promise`.

3. **C++20 Coroutines (Introductory)**
   - Coroutine keywords: `co_await`, `co_yield`, `co_return`.
   - Generator patterns for streaming data.

-  **Mini Project 5:** *Thread-Safe Work-Stealing Job Queue / Thread Pool*
  - Build a multi-threaded task runner using `std::jthread`, lock-free atomics, and condition variables to benchmark parallel computation against single-threaded baselines.

---

##  Capstone Project (Weeks 11-12)
Select **one** domain-relevant project to cement the skills:

1. **High-Performance Numerical / Power Grid Simulation Engine**
   - Nodal admittance / power flow solver using modern C++ sparse matrices, concepts for complex numbers, and multi-threaded parallel contingency screening.
2. **High-Throughput Lightweight HTTP/REST Server**
   - Async I/O or thread-pool server parsing requests with `std::string_view`, routing via modern STL, and handling JSON/data streams safely.
3. **Embedded / DSP Audio Signal Processing Pipeline**
   - Zero-allocation audio buffer processor leveraging `std::span`, SIMD optimizations, and C++20 ranges for realtime DSP effects.

---

##  Recommended Modern Resources (C++20/23 Focused)

- **Books:**
  - *A Tour of C++ (3rd Edition, C++20)* - Bjarne Stroustrup (concise overview).
  - *Effective Modern C++* - Scott Meyers (essential for move semantics and design).
  - *C++20 - The Complete Guide* - Nicolai M. Josuttis (exhaustive guide to concepts, ranges, coroutines).
  - *Professional C++ (6th Edition)* - Marc Gregoire (covers C++20 and C++23).

- **Interactive References & Tools:**
  - [Compiler Explorer (Godbolt)](https://godbolt.org/) - visualize assembly, test snippets instantly.
  - [cppreference.com](https://en.cppreference.com/) - authoritative standard documentation.
  - [Quick-Bench](https://quick-bench.com/) - microbenchmarking C++ code snippet performance.
