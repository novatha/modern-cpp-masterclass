#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <Eigen/Sparse>
#include <Eigen/Dense>
#include <Eigen/CholmodSupport>

using namespace Eigen;
using namespace std::chrono;

// Fungsi untuk membangkitkan grid 2D yang meniru Admittance Matrix
SparseMatrix<double> generate_grid_matrix(int grid_size) {
    int N = grid_size * grid_size;
    SparseMatrix<double> A(N, N);
    std::vector<Triplet<double>> triplets;
    
    // Diagonal dominan & off-diagonal negatif (seperti matriks B' FDXB)
    for (int i = 0; i < grid_size; ++i) {
        for (int j = 0; j < grid_size; ++j) {
            int node = i * grid_size + j;
            double diag = 0.0;
            
            // Connect to right
            if (j < grid_size - 1) {
                int right = i * grid_size + (j + 1);
                triplets.push_back({node, right, -1.0});
                triplets.push_back({right, node, -1.0});
                diag += 1.0;
            }
            // Connect to down
            if (i < grid_size - 1) {
                int down = (i + 1) * grid_size + j;
                triplets.push_back({node, down, -1.0});
                triplets.push_back({down, node, -1.0});
                diag += 1.0;
            }
            
            triplets.push_back({node, node, diag + 0.1}); // +0.1 to ensure SPD (grounding)
        }
    }
    
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();
    return A;
}

int main() {
    int grid_size = 100; // 100x100 = 10,000 buses
    int N = grid_size * grid_size;
    int num_n2_tests = 100; // Profile 100 N-2 contingencies
    
    std::cout << "=========================================================\n";
    std::cout << "|   WOODBURY & CHOLMOD N-2 PROFILING PROOF-OF-CONCEPT   |\n";
    std::cout << "=========================================================\n";
    std::cout << "Grid Size: " << N << " buses\n";
    std::cout << "Test N-2 Contingencies: " << num_n2_tests << "\n\n";

    SparseMatrix<double> B = generate_grid_matrix(grid_size);
    VectorXd P = VectorXd::Random(N); // Random power injections

    // Siapkan list kontingensi N-2 random
    std::mt19937 gen(42);
    std::uniform_int_distribution<> dis(0, N - 2);
    struct N2Pair { int n1, n2; double b1; int n3, n4; double b2; };
    std::vector<N2Pair> contingencies;
    for (int i=0; i<num_n2_tests; ++i) {
        int n1 = dis(gen); int n2 = n1 + 1; double b1 = -1.0; // Assume line has b=-1
        int n3 = dis(gen); int n4 = n3 + 1; double b2 = -1.0;
        contingencies.push_back({n1, n2, b1, n3, n4, b2});
    }

    // =========================================================================
    // 1. SimplicialLDLT + Refactorization (CURRENT BASELINE)
    // =========================================================================
    std::cout << "[1] Profiling: SimplicialLDLT + Refactorization\n";
    SimplicialLDLT<SparseMatrix<double>> solver_simp;
    solver_simp.compute(B);
    
    auto start = high_resolution_clock::now();
    for (const auto& c : contingencies) {
        SparseMatrix<double> B_mod = B;
        // Modifikasi elemen (simulate line trips) - inefficient direct coeff but fine for PoC
        B_mod.coeffRef(c.n1, c.n2) -= c.b1; B_mod.coeffRef(c.n2, c.n1) -= c.b1;
        B_mod.coeffRef(c.n1, c.n1) += c.b1; B_mod.coeffRef(c.n2, c.n2) += c.b1;
        
        B_mod.coeffRef(c.n3, c.n4) -= c.b2; B_mod.coeffRef(c.n4, c.n3) -= c.b2;
        B_mod.coeffRef(c.n3, c.n3) += c.b2; B_mod.coeffRef(c.n4, c.n4) += c.b2;
        
        solver_simp.compute(B_mod);
        VectorXd x = solver_simp.solve(P);
    }
    auto end = high_resolution_clock::now();
    double time_simp = duration_cast<milliseconds>(end - start).count();
    std::cout << "    Time: " << time_simp << " ms (" << (num_n2_tests / (time_simp/1000.0)) << " solves/sec)\n\n";

    // =========================================================================
    // 2. CholmodSupernodalLLT + Refactorization
    // =========================================================================
    std::cout << "[2] Profiling: CholmodSupernodalLLT + Refactorization\n";
    CholmodSupernodalLLT<SparseMatrix<double>> solver_cholmod;
    solver_cholmod.compute(B);
    
    start = high_resolution_clock::now();
    for (const auto& c : contingencies) {
        SparseMatrix<double> B_mod = B;
        B_mod.coeffRef(c.n1, c.n2) -= c.b1; B_mod.coeffRef(c.n2, c.n1) -= c.b1;
        B_mod.coeffRef(c.n1, c.n1) += c.b1; B_mod.coeffRef(c.n2, c.n2) += c.b1;
        
        B_mod.coeffRef(c.n3, c.n4) -= c.b2; B_mod.coeffRef(c.n4, c.n3) -= c.b2;
        B_mod.coeffRef(c.n3, c.n3) += c.b2; B_mod.coeffRef(c.n4, c.n4) += c.b2;
        
        solver_cholmod.compute(B_mod);
        VectorXd x = solver_cholmod.solve(P);
    }
    end = high_resolution_clock::now();
    double time_cholmod = duration_cast<milliseconds>(end - start).count();
    std::cout << "    Time: " << time_cholmod << " ms (" << (num_n2_tests / (time_cholmod/1000.0)) << " solves/sec)\n\n";

    // =========================================================================
    // 3. CholmodSupernodalLLT + Woodbury Matrix Identity (NO REFACTORIZATION)
    // =========================================================================
    std::cout << "[3] Profiling: CholmodSupernodalLLT + Woodbury (No Refactor)\n";
    start = high_resolution_clock::now();
    
    solver_cholmod.compute(B); // Factorize ONLY ONCE
    VectorXd x0 = solver_cholmod.solve(P); // Base solution
    
    for (const auto& c : contingencies) {
        // U is N x 2. Col 1: e_{n1} - e_{n2}. Col 2: e_{n3} - e_{n4}
        // Instead of building dense U, we just build the RHS directly.
        Matrix<double, Dynamic, 2> U = Matrix<double, Dynamic, 2>::Zero(N, 2);
        U(c.n1, 0) = 1.0; U(c.n2, 0) = -1.0;
        U(c.n3, 1) = 1.0; U(c.n4, 1) = -1.0;
        
        // C is 2x2: diag(-b1, -b2) -> Removing line implies subtracting its admittance
        Matrix2d C; C << -c.b1, 0, 0, -c.b2;
        Matrix2d C_inv = C.inverse();
        
        // Solve Z = B^{-1} U (2 forward/backward substitutions)
        Matrix<double, Dynamic, 2> Z = solver_cholmod.solve(U);
        
        // Woodbury core: (C^-1 + U^T Z)^-1
        Matrix2d WoodburyCore = C_inv + U.transpose() * Z;
        Matrix2d WoodburyCoreInv = WoodburyCore.inverse();
        
        // x_new = x0 - Z * WoodburyCoreInv * U^T * x0
        VectorXd x_new = x0 - Z * WoodburyCoreInv * (U.transpose() * x0);
    }
    end = high_resolution_clock::now();
    double time_woodbury = duration_cast<milliseconds>(end - start).count();
    std::cout << "    Time: " << time_woodbury << " ms (" << (num_n2_tests / (time_woodbury/1000.0)) << " solves/sec)\n";
    std::cout << "    Speedup over Baseline: " << (time_simp / time_woodbury) << "x\n";
    std::cout << "=========================================================\n";

    return 0;
}
