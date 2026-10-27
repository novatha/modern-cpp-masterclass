# GridGuard N-2: Rust Port (High-Performance Engine)

This directory contains the high-performance Rust implementation of the **Sherman-Morrison-Woodbury Rank-2 Inversion & Multi-Island N-2 Contingency Screening Engine** for power transmission systems.

## Key Architecture & Features

- **Sherman-Morrison-Woodbury Rank-2 Formula**: Updates the bus susceptance matrix inverse ($B_{\text{post}}^{-1}$) in $O(1)$ constant time per contingency using a $2 \times 2$ Woodbury core, avoiding full $O(N^3)$ matrix re-factorizations.
- **`faer` SIMD LDLT**: Leverages state-of-the-art Rust linear algebra with native hardware vectorization (AVX2, AVX-512, Apple Silicon NEON).
- **Rayon Multi-Threading**: Embarrassingly parallel evaluation across all CPU cores with work-stealing scheduling.
- **Disjoint Set Union (Union-Find)**: Instant $O(\alpha(N))$ topology islanding and blackout detection.
- **MATPOWER Case Parser**: Parses standard MATPOWER format (`.m`) directly.
- **JSON Telemetry**: Exports benchmark data matching the C++20 engine schema.

## Performance Benchmark Summary (Apple Silicon M-Series, 10 Threads)

| Benchmark System | N-2 Scenarios | Wall-Clock Time | Average Time / Case | Throughput |
| :--- | :--- | :--- | :--- | :--- |
| **IEEE 14-bus** | 190 (All) | **1.15 ms** | 42.27 µs | **165,385 solves/sec** |
| **IEEE 57-bus** | 3,160 (All) | **39.93 ms** | 124.87 µs | **79,138 solves/sec** |
| **IEEE 118-bus** | 5,000 | **111.53 ms** | 219.14 µs | **44,832 solves/sec** |

## Quick Start

### 1. Run Unit Tests
```bash
cargo test --lib -- --nocapture
```

### 2. Run N-2 Screening
```bash
# Build optimized release binary
cargo build --release

# Simulate all N-2 pairs on IEEE 14
./target/release/gridguard ../data/case14.m --all

# Simulate IEEE 57 with JSON export
./target/release/gridguard ../data/case57.m --all --json-out result_case57.json

# Simulate first 5,000 pairs on IEEE 118 with 8 threads
./target/release/gridguard ../data/case118.m --limit 5000 --threads 8
```
