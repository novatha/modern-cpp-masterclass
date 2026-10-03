#pragma once
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <stdexcept>

namespace grid_math {

/**
 * A lightweight, bounds-checked 2D Matrix buffer.
 * Demonstrates RAII, unique_ptr ownership, Move Semantics, and std::span.
 */
template <typename T>
class MatrixBuffer {
private:
    size_t rows_{0};
    size_t cols_{0};
    
    // RAII: Exclusive ownership of the memory block.
    // The memory is automatically freed when MatrixBuffer goes out of scope.
    std::unique_ptr<T[]> data_{nullptr}; 

public:
    // 1. Standard Constructor
    MatrixBuffer(size_t rows, size_t cols) 
        : rows_(rows), cols_(cols), 
          data_(std::make_unique<T[]>(rows * cols)) {}

    // 2. Rule of Five: Delete Copying
    // We forbid accidental copies to prevent massive hidden performance costs.
    MatrixBuffer(const MatrixBuffer&) = delete;
    MatrixBuffer& operator=(const MatrixBuffer&) = delete;

    // 3. Rule of Five: Enable Move Semantics
    // Efficiently steal the memory from a dying or temporary MatrixBuffer.
    MatrixBuffer(MatrixBuffer&& other) noexcept 
        : rows_(std::exchange(other.rows_, 0)),
          cols_(std::exchange(other.cols_, 0)),
          data_(std::move(other.data_)) {}

    MatrixBuffer& operator=(MatrixBuffer&& other) noexcept {
        if (this != &other) {
            rows_ = std::exchange(other.rows_, 0);
            cols_ = std::exchange(other.cols_, 0);
            data_ = std::move(other.data_); // Safely overwrites and frees current memory
        }
        return *this;
    }

    // Destructor is implicitly handled by std::unique_ptr! (Rule of Zero style)
    ~MatrixBuffer() = default;

    // 4. Modern access via std::span (Bounds-safe, non-owning view of a row)
    std::span<T> get_row(size_t row) {
        if (row >= rows_) throw std::out_of_range("Row index out of bounds");
        return std::span<T>(data_.get() + (row * cols_), cols_);
    }
    
    std::span<const T> get_row(size_t row) const {
        if (row >= rows_) throw std::out_of_range("Row index out of bounds");
        return std::span<const T>(data_.get() + (row * cols_), cols_);
    }

    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }
};

} // namespace grid_math
