# C++20 Modules (FILE_SET CXX_MODULES) and CMakePresets v6 require CMake 3.28+
cmake_minimum_required(VERSION 3.28)
project(ModernCppLearning VERSION 1.0.0 LANGUAGES CXX)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# NOTE: module scanning (CXX_SCAN_FOR_MODULES) is set per-target on the C++20
# module targets below, NOT globally. A global ON leaks into try_compile()
# (e.g. Catch2's compiler-flag checks) and breaks compilers without import
# graph discovery, GCC 13: "compiler does not provide a way to discover the
# import graph dependencies".

# C++20 modules demos/tests require a Clang-family compiler.
set(BUILD_CXX_MODULE_TARGETS ON)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "GNU ${CMAKE_CXX_COMPILER_VERSION}: skipping C++20 module targets (no module scanning support; use Clang)")
    set(BUILD_CXX_MODULE_TARGETS OFF)
endif()

# Export compile commands for language servers (clangd / ccls)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure strict compiler warnings & sanitizers
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
endif()

# Option for sanitizers (AddressSanitizer & UndefinedBehaviorSanitizer)
option(ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
if (ENABLE_SANITIZERS AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    message(STATUS "AddressSanitizer and UndefinedBehaviorSanitizer enabled")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Phase 1 executable
add_executable(phase1_basics src/phase1_basics.cpp)
# Mini-Project 1: Log Parsing (C++20 string_view & format)
add_executable(mini_project_1 src/main.cpp src/log_parser.cpp)
target_compile_definitions(mini_project_1 PRIVATE DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# ==============================================================================
# Phase 2: Memory Management & Automated Testing
# ==============================================================================

# FetchContent is the modern CMake way to grab dependencies without a global package manager
include(FetchContent)
message(STATUS "Fetching Catch2 (Unit Testing Framework)...")
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Mini-Project 2: MatrixBuffer Tests
add_executable(test_matrix tests/test_matrix.cpp)
target_include_directories(test_matrix PRIVATE include)
target_link_libraries(test_matrix PRIVATE Catch2::Catch2WithMain)

# Phase 3: EventDispatcher Concepts Tests
add_executable(test_event_dispatcher tests/test_event_dispatcher.cpp)
target_include_directories(test_event_dispatcher PRIVATE include)
target_link_libraries(test_event_dispatcher PRIVATE Catch2::Catch2WithMain)

# Phase 1/Log Parsing Tests
add_executable(test_log_parser tests/test_log_parser.cpp src/log_parser.cpp)
target_include_directories(test_log_parser PRIVATE src)
target_compile_definitions(test_log_parser PRIVATE DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_log_parser PRIVATE Catch2::Catch2WithMain)

# Phase 1 Module Tests (requires C++20 modules + Clang)
if (BUILD_CXX_MODULE_TARGETS)
    add_executable(test_grid_parser tests/test_grid_parser.cpp)
    target_compile_features(test_grid_parser PRIVATE cxx_std_20)
    target_sources(test_grid_parser
      PUBLIC
        FILE_SET CXX_MODULES FILES
        src/grid_parser.cppm
    )
    target_link_libraries(test_grid_parser PRIVATE Catch2::Catch2WithMain)
    set_property(TARGET test_grid_parser PROPERTY CXX_SCAN_FOR_MODULES ON)
endif()

# Phase 5: Concurrency Tests
add_executable(test_concurrency tests/test_concurrency.cpp)
target_link_libraries(test_concurrency PRIVATE Catch2::Catch2WithMain)

# Phase 6: Advanced/C++23 Tests
add_executable(test_phase6 tests/test_phase6.cpp)
target_compile_features(test_phase6 PRIVATE cxx_std_23)
target_link_libraries(test_phase6 PRIVATE Catch2::Catch2WithMain)

# Enable CTest runner
enable_testing()
include(Catch)
catch_discover_tests(test_matrix)
catch_discover_tests(test_event_dispatcher)
catch_discover_tests(test_log_parser)
if (BUILD_CXX_MODULE_TARGETS)
    catch_discover_tests(test_grid_parser)
endif()
catch_discover_tests(test_concurrency)
catch_discover_tests(test_phase6)

# ==============================================================================
# Phase 3: Generic Programming & Concepts
# ==============================================================================
add_executable(mini_project_3 src/phase3_main.cpp)
target_include_directories(mini_project_3 PRIVATE include)

# ==============================================================================
# Phase 4: Modern STL, Ranges & Functional Pipelines
# ==============================================================================
add_executable(mini_project_4 src/phase4_ranges.cpp)
target_include_directories(mini_project_4 PRIVATE src)

# Phase 4: Ranges Tests
add_executable(test_ranges tests/test_ranges.cpp)
target_include_directories(test_ranges PRIVATE src)
target_link_libraries(test_ranges PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(test_ranges)

# ==============================================================================
# Phase 5: Concurrency, Multithreading & Performance
# ==============================================================================
add_executable(mini_project_5 src/phase5_concurrency.cpp)

# ==============================================================================
# CAPSTONE PROJECT: Parallel N-1 Contingency Engine
# ==============================================================================
add_executable(capstone_project src/capstone_main.cpp)

# ==============================================================================
# Phase 6: Advanced Hardware Sympathy & C++23
# ==============================================================================
add_executable(mini_project_6 src/phase6_advanced.cpp)
target_compile_features(mini_project_6 PRIVATE cxx_std_23)

# ==============================================================================
# Phase 1 Re-do: True C++20 Modules with LLVM
# ==============================================================================
if (BUILD_CXX_MODULE_TARGETS)
    add_executable(phase1_modules src/phase1_modules.cpp)
    target_compile_features(phase1_modules PRIVATE cxx_std_20)
    target_sources(phase1_modules
      PUBLIC
        FILE_SET CXX_MODULES FILES
        src/grid_parser.cppm
    )
    set_property(TARGET phase1_modules PROPERTY CXX_SCAN_FOR_MODULES ON)
endif()
