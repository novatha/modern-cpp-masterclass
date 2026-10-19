#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <regex>
#include <cmath>
#include <charconv>
#include <chrono>
#include <unordered_map>
#include <iomanip>
#include <Eigen/Dense>
#include <Eigen/Sparse>

// Struktur Data Power Grid (RAII & Data-Oriented)
struct Bus {
    int id;
    int type; // 1 = PQ, 2 = PV, 3 = Ref
    double Pd{0.0}; // MW load
    double Pg{0.0}; // MW generation
    double theta{0.0}; // rad (Output)
};

struct Branch {
    int from;
    int to;
    double r{0.0};
    double x{0.0}; // Reactance p.u.
    double rateA{0.0}; // MVA thermal limit (0 = unconstrained)
    int status{1}; // 1 = in-service, 0 = out-of-service
    
    // Hasil kalkulasi aliran daya
    double P_from_to{0.0}; // Aliran daya aktif (MW)
    double loading_pct{0.0}; // Persentase pembebanan (%)
    bool is_overloaded{false};
};

struct Gen {
    int bus;
    double Pg{0.0};
    int status{1};
};

class MatpowerParser {
public:
    std::vector<Bus> buses;
    std::vector<Branch> branches;
    std::vector<Gen> generators;
    double baseMVA = 100.0;

    bool load(const std::string& filepath) {
        // 1. Baca seluruh file ke dalam RAM sekaligus (Zero I/O bottleneck)
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Gagal membuka file: " << filepath << "\n";
            return false;
        }
        size_t size = file.tellg();
        std::string buffer(size, '\0');
        file.seekg(0);
        file.read(&buffer[0], size);

        // 2. Gunakan std::string_view untuk zero-allocation parsing
        std::string_view content(buffer);
        bool in_bus = false;
        bool in_branch = false;
        bool in_gen = false;

        size_t line_start = 0;
        while (line_start < content.size()) {
            size_t line_end = content.find('\n', line_start);
            if (line_end == std::string_view::npos) line_end = content.size();
            
            std::string_view line = content.substr(line_start, line_end - line_start);
            line_start = line_end + 1;

            // Abaikan komentar MATLAB (%)
            size_t comment_pos = line.find('%');
            if (comment_pos != std::string_view::npos) {
                line = line.substr(0, comment_pos);
            }

            // Deteksi batas matriks
            if (line.find("mpc.bus = [") != std::string_view::npos) { in_bus = true; continue; }
            if (line.find("mpc.gen = [") != std::string_view::npos) { in_gen = true; continue; }
            if (line.find("mpc.branch = [") != std::string_view::npos) { in_branch = true; continue; }
            if (line.find("];") != std::string_view::npos) { in_bus = false; in_gen = false; in_branch = false; continue; }

            if (!in_bus && !in_branch && !in_gen) continue;

            // 3. Ekstraksi angka cepat dengan std::from_chars (C++17)
            std::vector<double> tokens;
            const char* p = line.data();
            const char* end = line.data() + line.size();

            while (p < end) {
                if (*p == ' ' || *p == '\t' || *p == '\r' || *p == ';' || *p == ',') { 
                    p++; continue; 
                }
                
                double value;
                auto [ptr, ec] = std::from_chars(p, end, value);
                if (ec == std::errc()) {
                    tokens.push_back(value);
                    p = ptr;
                } else {
                    p++;
                }
            }

            // Parsing Bus
            if (in_bus && tokens.size() >= 3) {
                Bus b;
                b.id = static_cast<int>(tokens[0]);
                b.type = static_cast<int>(tokens[1]);
                b.Pd = tokens[2];
                buses.push_back(b);
            }
            // Parsing Generator (mpc.gen)
            else if (in_gen && tokens.size() >= 2) {
                Gen g;
                g.bus = static_cast<int>(tokens[0]);
                g.Pg = tokens[1];
                if (tokens.size() >= 8) {
                    g.status = static_cast<int>(tokens[7]);
                }
                if (g.status > 0) {
                    generators.push_back(g);
                }
            }
            // Parsing Branch (mpc.branch)
            else if (in_branch && tokens.size() >= 4) {
                Branch br;
                br.from = static_cast<int>(tokens[0]);
                br.to = static_cast<int>(tokens[1]);
                br.r = tokens[2];
                br.x = tokens[3];
                if (tokens.size() >= 6) {
                    br.rateA = tokens[5];
                }
                if (tokens.size() >= 11) {
                    br.status = static_cast<int>(tokens[10]);
                }
                branches.push_back(br);
            }
        }
        return true;
    }
};

void run_dc_powerflow(MatpowerParser& grid) {
    int n = static_cast<int>(grid.buses.size());

    // MAPPING BUS ID -> INDEX (0 to N-1)
    std::unordered_map<int, int> id_to_idx;
    for (int i = 0; i < n; ++i) {
        id_to_idx[grid.buses[i].id] = i;
    }

    // Injeksi generator ke setiap bus
    for (const auto& g : grid.generators) {
        auto it = id_to_idx.find(g.bus);
        if (it != id_to_idx.end()) {
            grid.buses[it->second].Pg += g.Pg;
        }
    }

    // Identifikasi Reference Bus
    int ref_bus_idx = -1;
    for (int i = 0; i < n; ++i) {
        if (grid.buses[i].type == 3) {
            ref_bus_idx = i;
            break;
        }
    }
    if (ref_bus_idx == -1) {
        std::cerr << "Reference bus tidak ditemukan!\n";
        return;
    }

    // Pemetaan indeks penuh (0..N-1) ke indeks tereduksi (0..N-2)
    std::vector<int> full_to_red(n, -1);
    int r_counter = 0;
    for (int i = 0; i < n; ++i) {
        if (i != ref_bus_idx) {
            full_to_red[i] = r_counter++;
        }
    }

    // 1. Bentuk Matriks B_red Sparse menggunakan Triplet
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(grid.branches.size() * 4);

    for (const auto& br : grid.branches) {
        if (br.status == 0) continue; // Skip saluran mati (out-of-service / kontingensi)

        auto it_f = id_to_idx.find(br.from);
        auto it_t = id_to_idx.find(br.to);
        if (it_f == id_to_idx.end() || it_t == id_to_idx.end()) continue;

        int f = it_f->second;
        int t = it_t->second;
        double b_ij = 1.0 / br.x;

        int f_red = full_to_red[f];
        int t_red = full_to_red[t];

        if (f_red != -1) triplets.emplace_back(f_red, f_red, b_ij);
        if (t_red != -1) triplets.emplace_back(t_red, t_red, b_ij);
        if (f_red != -1 && t_red != -1) {
            triplets.emplace_back(f_red, t_red, -b_ij);
            triplets.emplace_back(t_red, f_red, -b_ij);
        }
    }

    Eigen::SparseMatrix<double> B_red(n - 1, n - 1);
    B_red.setFromTriplets(triplets.begin(), triplets.end());
    B_red.makeCompressed();

    // 2. Bentuk Vektor Injeksi Daya Bersih P = (P_gen - P_load)
    Eigen::VectorXd P_red = Eigen::VectorXd::Zero(n - 1);
    double total_load_mw = 0.0;
    double total_gen_non_ref_mw = 0.0;

    for (int i = 0; i < n; ++i) {
        total_load_mw += grid.buses[i].Pd;
        if (i != ref_bus_idx) {
            total_gen_non_ref_mw += grid.buses[i].Pg;
            double net_injection_pu = (grid.buses[i].Pg - grid.buses[i].Pd) / grid.baseMVA;
            P_red[full_to_red[i]] = net_injection_pu;
        }
    }

    // Reference bus menyuplai sisa kekurangan beban (Slack)
    grid.buses[ref_bus_idx].Pg = total_load_mw - total_gen_non_ref_mw;

    // 3. Solve Linear System: B_red * Theta_red = P_red
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(B_red);
    if (solver.info() != Eigen::Success) {
        std::cerr << "Faktorisasi Sparse Cholesky/LDLT gagal!\n";
        return;
    }

    Eigen::VectorXd Theta_red = solver.solve(P_red);
    if (solver.info() != Eigen::Success) {
        std::cerr << "Solving Sparse system gagal!\n";
        return;
    }

    // 4. Rekonstruksi Theta lengkap (N)
    Eigen::VectorXd Theta = Eigen::VectorXd::Zero(n);
    for (int i = 0; i < n; ++i) {
        if (i != ref_bus_idx) {
            Theta(i) = Theta_red(full_to_red[i]);
            grid.buses[i].theta = Theta(i);
        } else {
            grid.buses[i].theta = 0.0;
        }
    }

    // 5. HITUNG ALIRAN DAYA PADA SETIAP SALURAN (Branch Power Flow)
    // Formula DC: P_ij = (theta_i - theta_j) / x_ij * baseMVA
    int overloaded_count = 0;
    for (auto& br : grid.branches) {
        if (br.status == 0) continue;

        int f = id_to_idx[br.from];
        int t = id_to_idx[br.to];

        br.P_from_to = (Theta(f) - Theta(t)) / br.x * grid.baseMVA;

        if (br.rateA > 0.0) {
            br.loading_pct = (std::abs(br.P_from_to) / br.rateA) * 100.0;
            if (br.loading_pct > 100.0) {
                br.is_overloaded = true;
                overloaded_count++;
            }
        }
    }

    // 6. Cetak Hasil Bus (Persis MATPOWER)
    std::cout << "\n================================================================================\n";
    std::cout << "|     DC Power Flow Results                                                    |\n";
    std::cout << "================================================================================\n";
    std::cout << "Bus Data                                                                        \n";
    std::cout << "================================================================================\n";
    std::cout << " Bus      Voltage          Generation             Load          \n";
    std::cout << "  #   Mag(pu) Ang(deg)   P (MW)   Q (MVAr)   P (MW)   Q (MVAr)  \n";
    std::cout << "----  ------- --------  --------  --------  --------  --------  \n";

    int print_limit = std::min(n, 10);
    for (int i = 0; i < print_limit; ++i) {
        double angle_deg = Theta(i) * 180.0 / M_PI;
        printf("%4d %8.3f %8.3f %9.2f %9.2f %9.2f %9.2f\n", 
               grid.buses[i].id, 1.000, angle_deg, 
               grid.buses[i].Pg, 0.0, 
               grid.buses[i].Pd, 0.0);
    }
    if (n > 10) std::cout << " ... (menampilkan 10 dari " << n << " bus) ...\n";

    // 7. Cetak Hasil Branch Flow & Line Loading
    std::cout << "\n================================================================================\n";
    std::cout << "Branch Data (Line Flows & Thermal Loading)                                      \n";
    std::cout << "================================================================================\n";
    std::cout << "Branch  From   To     From P     To P     RateA    Loading   Status             \n";
    std::cout << "  #     Bus    Bus     (MW)      (MW)     (MW)       (%)                        \n";
    std::cout << "------  ----  -----  --------  --------  -------  ---------  -------------------\n";

    int branch_print_limit = std::min(static_cast<int>(grid.branches.size()), 15);
    for (int i = 0; i < branch_print_limit; ++i) {
        const auto& br = grid.branches[i];
        if (br.status == 0) {
            printf("%5d   %4d  %5d   [OUT OF SERVICE]\n", i + 1, br.from, br.to);
            continue;
        }

        std::string status_str = "OK";
        if (br.is_overloaded) {
            status_str = ">> OVERLOAD! <<";
        } else if (br.rateA == 0.0) {
            status_str = "Unconstrained";
        }

        printf("%5d   %4d  %5d  %8.2f  %8.2f  %7.1f  %8.2f%%   %s\n",
               i + 1, br.from, br.to,
               br.P_from_to, -br.P_from_to,
               br.rateA, br.loading_pct,
               status_str.c_str());
    }
    if (grid.branches.size() > 15) {
        std::cout << " ... (menampilkan 15 dari " << grid.branches.size() << " cabang) ...\n";
    }

    // 8. Ringkasan Status Sistem (Security & Overload Summary)
    std::cout << "\n================================================================================\n";
    std::cout << "| Grid Security Summary: " << (overloaded_count > 0 ? "⚠️  PERINGATAN OVERLOAD!" : "✅ AMAN (NORMAL)") << "\n";
    std::cout << "| Total Saluran Overload (>100%): " << overloaded_count << " dari " << grid.branches.size() << " cabang\n";
    std::cout << "================================================================================\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Penggunaan: " << argv[0] << " <matpower_case.m>\n";
        return 1;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    MatpowerParser parser;
    if (parser.load(argv[1])) {
        auto parse_time = std::chrono::high_resolution_clock::now();
        std::cout << "Berhasil memuat " << parser.buses.size() << " bus, " 
                  << parser.generators.size() << " generator, dan "
                  << parser.branches.size() << " cabang dalam "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(parse_time - start_time).count() 
                  << " ms.\n";

        run_dc_powerflow(parser);

        auto end_time = std::chrono::high_resolution_clock::now();
        std::cout << "Waktu Solve Total: " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - parse_time).count() 
                  << " ms.\n";
    }

    return 0;
}
