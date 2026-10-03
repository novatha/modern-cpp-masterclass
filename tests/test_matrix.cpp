#include <catch2/catch_test_macros.hpp>
#include "matrix_buffer.hpp"
#include <vector>

TEST_CASE("MatrixBuffer RAII and Initialization", "[matrix]") {
    // Memory is acquired upon initialization (RAII)
    grid_math::MatrixBuffer<double> mat(10, 5);
    
    REQUIRE(mat.rows() == 10);
    REQUIRE(mat.cols() == 5);
    
    auto row0 = mat.get_row(0);
    REQUIRE(row0.size() == 5);
    
    row0[0] = 42.5;
    REQUIRE(mat.get_row(0)[0] == 42.5);
    
    // Bounds checking via std::span & custom check
    REQUIRE_THROWS_AS(mat.get_row(99), std::out_of_range);
}

TEST_CASE("MatrixBuffer Move Semantics", "[matrix]") {
    grid_math::MatrixBuffer<int> mat1(5, 5);
    mat1.get_row(0)[0] = 99;

    // Move construct mat2 from mat1. 
    // ZERO memory allocation happens here; mat2 just steals mat1's pointer.
    grid_math::MatrixBuffer<int> mat2(std::move(mat1));
    
    // mat1 should be empty and safe to destroy
    REQUIRE(mat1.rows() == 0);
    REQUIRE(mat1.cols() == 0);
    
    // mat2 should now own the data
    REQUIRE(mat2.rows() == 5);
    REQUIRE(mat2.cols() == 5);
    REQUIRE(mat2.get_row(0)[0] == 99);
}

TEST_CASE("std::span prevents buffer decay", "[matrix]") {
    grid_math::MatrixBuffer<int> mat(3, 3);
    auto row1 = mat.get_row(1); // row1 is a std::span<int>
    
    // std::span has built-in bounds knowledge unlike raw pointers (int*)
    REQUIRE(row1.size() == 3);
    row1[0] = 1;
    row1[1] = 2;
    row1[2] = 3;
    
    // We can iterate over the span cleanly
    int sum = 0;
    for (int val : row1) {
        sum += val;
    }
    REQUIRE(sum == 6);
}
