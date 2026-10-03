# Modern C++ Masterclass — Power Systems Edition

Kursus C++20/23 bertema **Power Systems / N-1 Contingency Analysis** untuk Universitas Bengkulu.

## Prasyarat

| Tool | Versi Minimum | Catatan |
|------|---------------|---------|
| CMake | 3.28 | Required untuk `FILE_SET CXX_MODULES` |
| Clang | 17 | `<format>`, modules, `std::expected` |
| Ninja | 1.11 | Build system default |
| Python | 3.10 | Untuk skrip ekstraksi snippet |

> **macOS**: `brew install llvm@18 ninja cmake python` → `export PATH="/opt/homebrew/opt/llvm@18/bin:$PATH"`
>
> **Ubuntu**: `apt install clang-18 libc++-18-dev ninja-build cmake python3`

## Quick Start

```bash
# 1. Clone
git clone <repo-url>
cd cpp

# 2. Configure & Build (Release)
cmake --preset=release
ninja -C build

# 3. Run all targets
./build/phase1_basics
./build/mini_project_1
./build/mini_project_3
./build/mini_project_4
./build/mini_project_5
./build/mini_project_6
./build/capstone_project
./build/phase1_modules

# 4. Run tests
ctest --test-dir build --output-on-failure
```

## Struktur Proyek

```
├── CMakeLists.txt              # Build config (C++20/23, FetchContent Catch2)
├── CMakePresets.json           # Preset configure/build/test/release (CMake 3.28+)
├── .github/workflows/ci.yml    # CI: Ubuntu (Clang/GCC) + macOS (Clang)
├── assets/                     # Logo UB untuk Beamer slides
├── include/
│   ├── matrix_buffer.hpp       # RAII, move, span — Phase 2
│   └── event_dispatcher.hpp    # Concepts, type erasure — Phase 3
├── src/
│   ├── phase1_basics.cpp       # string_view, format, consteval, structured binding
│   ├── phase1_modules.cpp      # C++20 Modules (import grid_parser)
│   ├── grid_parser.cppm        # Module unit
│   ├── log_parser.hpp/.cpp     # string_view parsing — Mini Project 1
│   ├── main.cpp                # Mini Project 1 entry
│   ├── phase3_main.cpp         # Concepts + EventBus demo
│   ├── phase4_pipeline.hpp     # Ranges pipeline + optional (reusable, di-test)
│   ├── phase4_ranges.cpp       # Demo Mini Project 4
│   ├── phase5_concurrency.cpp  # jthread, stop_token, atomic, scoped_lock
│   ├── phase6_advanced.cpp     # expected, SoA vs AoS benchmark
│   └── capstone_main.cpp       # Parallel N-1 Contingency Engine
├── tests/
│   ├── test_matrix.cpp         # Catch2 tests for matrix_buffer.hpp
│   ├── test_event_dispatcher.cpp
│   ├── test_log_parser.cpp
│   ├── test_grid_parser.cpp    # C++20 modules test
│   ├── test_ranges.cpp         # Phase 4 ranges/optional tests
│   ├── test_concurrency.cpp
│   └── test_phase6.cpp         # C++23 expected + SoA layout tests
├── data/
│   └── sample_grid_logs.txt    # Sample log data
├── docs/
│   ├── slides/                 # Beamer slides, worksheet, problem set
│   └── snippets/               # Auto-generated oleh scripts/extract_snippets.py
└── scripts/
    └── extract_snippets.py     # Ekstrak // SNIPPET: ... ke LaTeX (minted)
```

## Fase Pembelajaran

| Fase | Topik Utama | Target Build | File Utama |
|------|-------------|--------------|------------|
| 1 | `string_view`, `format`, `consteval`, structured binding, modules | `phase1_basics`, `phase1_modules`, `mini_project_1`, `test_log_parser`, `test_grid_parser` | `src/phase1_*.cpp`, `src/log_parser.*` |
| 2 | RAII, Move Semantics, `unique_ptr`, `span`, Testing | `test_matrix` | `include/matrix_buffer.hpp`, `tests/test_matrix.cpp` |
| 3 | Concepts, Type Erasure, `if constexpr` | `mini_project_3`, `test_event_dispatcher` | `include/event_dispatcher.hpp`, `src/phase3_main.cpp` |
| 4 | Ranges, Views, Pipelines, `optional` | `mini_project_4`, `test_ranges` | `src/phase4_pipeline.hpp`, `src/phase4_ranges.cpp` |
| 5 | `jthread`, `stop_token`, `atomic`, `scoped_lock` | `mini_project_5`, `test_concurrency` | `src/phase5_concurrency.cpp` |
| 6 | `expected`, SoA vs AoS, Hardware Sympathy | `mini_project_6`, `test_phase6` | `src/phase6_advanced.cpp` |
| Capstone | N-1 Parallel Contingency Screening | `capstone_project` | `src/capstone_main.cpp` |

## CMake Presets

```bash
cmake --preset=release      # Release build (default)
cmake --preset=debug        # Debug + sanitizers
cmake --preset=ci           # CI-optimized
ninja -C build              # Build
ctest --test-dir build      # Test
```

## Continuous Integration

CI menjalankan:
- **Ubuntu**: Clang 18 + GCC 13 (Release + ASan/UBSan)
- **macOS**: Homebrew LLVM Clang (Release)
- Matrix memverifikasi portabilitas compiler

## Menulis Materi Ajar (Slides/Worksheet/Problem Set)

```bash
# Ekstrak snippet berlabel ke LaTeX (scan src/ dan include/)
python3 scripts/extract_snippets.py --src src include --out docs/snippets/ --slides-dir docs/slides/

# Kompilasi slides (memerlukan logo UB di assets/)
cd docs && latexmk -pdf modern_cpp_masterclass.tex
```

## Lisensi

MIT License — lihat `LICENSE` (akan ditambahkan).

## Kontribusi

1. Fork → branch `fix/<topik>` atau `feat/<topik>`
2. `cmake --preset=release && ninja -C build && ctest --test-dir build`
3. PR → CI hijau → review → merge

---

**Catatan**: Proyek ini dalam pengembangan aktif. Beberapa fase (khususnya benchmark Phase 6) masih diperbaiki untuk akurasi pedagogis.