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
# POWER FLOW ENGINE (Eigen + DC Power Flow)
# ==============================================================================
message(STATUS "Fetching Eigen3 (Linear Algebra)...")
FetchContent_Declare(
  Eigen3
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        3.4.0
)
FetchContent_MakeAvailable(Eigen3)

add_executable(dc_powerflow src/dc_powerflow.cpp)
target_compile_features(dc_powerflow PRIVATE cxx_std_20)
target_link_libraries(dc_powerflow PRIVATE Eigen3::Eigen)

add_executable(ac_powerflow src/ac_powerflow.cpp)
target_compile_features(ac_powerflow PRIVATE cxx_std_20)
target_link_libraries(ac_powerflow PRIVATE Eigen3::Eigen)

# Optional SuiteSparse KLU Support
find_path(SUITESPARSE_INCLUDE_DIR NAMES suitesparse/klu.h PATHS /opt/homebrew/include /usr/local/include)
find_path(KLU_INCLUDE_DIR NAMES klu.h PATHS /opt/homebrew/include/suitesparse /usr/local/include/suitesparse)
find_library(KLU_LIB NAMES klu PATHS /opt/homebrew/lib /usr/local/lib)
find_library(BTF_LIB NAMES btf PATHS /opt/homebrew/lib /usr/local/lib)
find_library(COLAMD_LIB NAMES colamd PATHS /opt/homebrew/lib /usr/local/lib)
find_library(AMD_LIB NAMES amd PATHS /opt/homebrew/lib /usr/local/lib)
find_library(SUITESPARSECONFIG_LIB NAMES suitesparseconfig PATHS /opt/homebrew/lib /usr/local/lib)

if (KLU_LIB AND KLU_INCLUDE_DIR)
    message(STATUS "Found SuiteSparse KLU: ${KLU_LIB}")
    target_compile_definitions(ac_powerflow PRIVATE HAVE_KLU=1)
    target_include_directories(ac_powerflow PRIVATE ${KLU_INCLUDE_DIR} ${SUITESPARSE_INCLUDE_DIR})
    target_link_libraries(ac_powerflow PRIVATE ${KLU_LIB} ${BTF_LIB} ${COLAMD_LIB} ${AMD_LIB} ${SUITESPARSECONFIG_LIB})
endif()

add_executable(fast_decoupled src/fast_decoupled.cpp)
target_compile_features(fast_decoupled PRIVATE cxx_std_20)
target_link_libraries(fast_decoupled PRIVATE Eigen3::Eigen)

add_executable(contingency_analysis src/contingency_analysis.cpp)
target_compile_features(contingency_analysis PRIVATE cxx_std_20)
target_link_libraries(contingency_analysis PRIVATE Eigen3::Eigen)
if (KLU_LIB AND KLU_INCLUDE_DIR)
    target_compile_definitions(contingency_analysis PRIVATE HAVE_KLU=1)
    target_include_directories(contingency_analysis PRIVATE ${KLU_INCLUDE_DIR} ${SUITESPARSE_INCLUDE_DIR})
    target_link_libraries(contingency_analysis PRIVATE ${KLU_LIB} ${BTF_LIB} ${COLAMD_LIB} ${AMD_LIB} ${SUITESPARSECONFIG_LIB})
endif()

add_executable(contingency_n2 src/contingency_n2.cpp)
target_compile_features(contingency_n2 PRIVATE cxx_std_20)
target_link_libraries(contingency_n2 PRIVATE Eigen3::Eigen)
if (KLU_LIB AND KLU_INCLUDE_DIR)
    target_compile_definitions(contingency_n2 PRIVATE HAVE_KLU=1)
    target_include_directories(contingency_n2 PRIVATE ${KLU_INCLUDE_DIR} ${SUITESPARSE_INCLUDE_DIR})
    target_link_libraries(contingency_n2 PRIVATE ${KLU_LIB} ${BTF_LIB} ${COLAMD_LIB} ${AMD_LIB} ${SUITESPARSECONFIG_LIB})
endif()
find_library(CHOLMOD_LIB NAMES cholmod PATHS /opt/homebrew/lib /usr/local/lib)
if (CHOLMOD_LIB)
    target_link_libraries(contingency_n2 PRIVATE ${CHOLMOD_LIB})
endif()

add_executable(contingency_n2_cholesky src/contingency_n2_cholesky.cpp)
target_compile_features(contingency_n2_cholesky PRIVATE cxx_std_20)
target_link_libraries(contingency_n2_cholesky PRIVATE Eigen3::Eigen)

add_executable(contingency_n2_woodbury src/contingency_n2_woodbury.cpp)
target_compile_features(contingency_n2_woodbury PRIVATE cxx_std_20)
target_link_libraries(contingency_n2_woodbury PRIVATE Eigen3::Eigen)
if (CHOLMOD_LIB)
    target_compile_definitions(contingency_n2_woodbury PRIVATE USE_CHOLMOD)
    target_include_directories(contingency_n2_woodbury PRIVATE /opt/homebrew/include/suitesparse /opt/homebrew/include)
    target_link_libraries(contingency_n2_woodbury PRIVATE ${CHOLMOD_LIB})
endif()



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
