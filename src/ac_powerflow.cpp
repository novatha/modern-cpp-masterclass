#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <regex>
#include <cmath>
#include <complex>
#include <charconv>
#include <chrono>
#include <unordered_map>
#include <iomanip>
#include <algorithm>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#ifdef HAVE_KLU
#include <Eigen/KLUSupport>
#endif

// Profiler Timer Data
struct ProfileMetrics {
    double parse_us{0.0};
    double ybus_us{0.0};
    double power_inj_us{0.0};
    double mismatch_us{0.0};
    double jac_assembly_us{0.0};
    double lu_symbolic_us{0.0}; // analyzePattern (cukup 1x per topologi)
    double lu_numeric_us{0.0};  // factorize (tiap iterasi)
    double lu_solve_us{0.0};
    double state_update_us{0.0};
    double qlim_check_us{0.0};
    double postproc_us{0.0};
    double total_solve_us{0.0};
};

// Struktur Data AC Power Grid
struct Bus {
    int id;
    int type; // 1 = PQ, 2 = PV, 3 = Ref
    int original_type{1};
    double Pd{0.0}; // MW load
    double Qd{0.0}; // MVAr load
    double Gs{0.0}; // MW shunt at V = 1.0
    double Bs{0.0}; // MVAr shunt at V = 1.0
    double Vm{1.0}; // Voltage magnitude (p.u.)
    double Va{0.0}; // Voltage angle (rad)
    
    // Total Generation & Batas Reaktif di bus ini
    double Pg{0.0}; // MW
    double Qg{0.0}; // MVAr
    double Qmax{0.0}; // MVAr
    double Qmin{0.0}; // MVAr
    double Vg{1.0}; // Voltage setpoint generator
    bool has_gen{false};
    bool q_limited{false};
};

struct Branch {
    int from;
    int to;
    double r{0.0};
    double x{0.0};
    double b{0.0}; // Total charging susceptance (p.u.)
    double rateA{0.0}; // MVA rating
    double ratio{1.0}; // Transformer tap ratio (1.0 = normal line)
    double angle{0.0}; // Transformer phase shift (deg)
    int status{1}; // 1 = in-service, 0 = out-of-service

    // Hasil perhitungan AC
    double P_from{0.0};
    double Q_from{0.0};
    double P_to{0.0};
    double Q_to{0.0};
    double Ploss{0.0};
    double Qloss{0.0};
    double S_max{0.0};
    double loading_pct{0.0};
    bool is_overloaded{false};
};

struct Gen {
    int bus;
    double Pg{0.0};
    double Qg{0.0};
    double Qmax{9999.0};
    double Qmin{-9999.0};
    double Vg{1.0};
    int status{1};
};

class MatpowerACParser {
public:
    std::vector<Bus> buses;
    std::vector<Branch> branches;
    std::vector<Gen> generators;
    double baseMVA = 100.0;

    bool load(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Gagal membuka file: " << filepath << "\n";
            return false;
        }
        size_t size = file.tellg();
        std::string buffer(size, '\0');
        file.seekg(0);
        file.read(&buffer[0], size);

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

            size_t comment_pos = line.find('%');
            if (comment_pos != std::string_view::npos) {
                line = line.substr(0, comment_pos);
            }

            if (line.find("mpc.bus = [") != std::string_view::npos) { in_bus = true; continue; }
            if (line.find("mpc.gen = [") != std::string_view::npos) { in_gen = true; continue; }
            if (line.find("mpc.branch = [") != std::string_view::npos) { in_branch = true; continue; }
            if (line.find("];") != std::string_view::npos) { in_bus = false; in_gen = false; in_branch = false; continue; }

            if (!in_bus && !in_branch && !in_gen) continue;

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

            // Parsing Bus Data
            if (in_bus && tokens.size() >= 3) {
                Bus b;
                b.id = static_cast<int>(tokens[0]);
                b.type = static_cast<int>(tokens[1]);
                b.original_type = b.type;
                b.Pd = tokens[2];
                if (tokens.size() >= 4) b.Qd = tokens[3];
                if (tokens.size() >= 5) b.Gs = tokens[4];
                if (tokens.size() >= 6) b.Bs = tokens[5];
                if (tokens.size() >= 8) b.Vm = tokens[7];
                if (tokens.size() >= 9) b.Va = tokens[8] * M_PI / 180.0;
                buses.push_back(b);
            }
            // Parsing Generator Data
            else if (in_gen && tokens.size() >= 2) {
                Gen g;
                g.bus = static_cast<int>(tokens[0]);
                g.Pg = tokens[1];
                if (tokens.size() >= 3) g.Qg = tokens[2];
                if (tokens.size() >= 4) g.Qmax = tokens[3];
                if (tokens.size() >= 5) g.Qmin = tokens[4];
                if (tokens.size() >= 6) g.Vg = tokens[5];
                if (tokens.size() >= 8) g.status = static_cast<int>(tokens[7]);
                if (g.status > 0) {
                    generators.push_back(g);
                }
            }
            // Parsing Branch Data
            else if (in_branch && tokens.size() >= 4) {
                Branch br;
                br.from = static_cast<int>(tokens[0]);
                br.to = static_cast<int>(tokens[1]);
                br.r = tokens[2];
                br.x = tokens[3];
                if (tokens.size() >= 5) br.b = tokens[4];
                if (tokens.size() >= 6) br.rateA = tokens[5];
                if (tokens.size() >= 9 && tokens[8] != 0.0) br.ratio = tokens[8];
                if (tokens.size() >= 10) br.angle = tokens[9];
                if (tokens.size() >= 11) br.status = static_cast<int>(tokens[10]);
                branches.push_back(br);
            }
        }
        return true;
    }
};

template <typename LinearSolver>
void run_ac_powerflow(MatpowerACParser& grid, bool enforce_q_limits, ProfileMetrics& prof, const std::string& solver_name, double tolerance = 1e-6, int max_iterations = 25) {
    using Clock = std::chrono::high_resolution_clock;
    int n = static_cast<int>(grid.buses.size());

    auto t_start_total = Clock::now();

    // 1. Mapping Bus ID -> Index (0 sampai N-1)
    std::unordered_map<int, int> id_to_idx;
    for (int i = 0; i < n; ++i) {
        id_to_idx[grid.buses[i].id] = i;
        grid.buses[i].original_type = grid.buses[i].type;
        grid.buses[i].Qmax = 0.0;
        grid.buses[i].Qmin = 0.0;
    }

    // 2. Hubungkan Generator ke Bus & Agregasikan Limit Q
    for (const auto& g : grid.generators) {
        auto it = id_to_idx.find(g.bus);
        if (it != id_to_idx.end()) {
            int idx = it->second;
            grid.buses[idx].Pg += g.Pg;
            grid.buses[idx].Qg += g.Qg;
            grid.buses[idx].Qmax += g.Qmax;
            grid.buses[idx].Qmin += g.Qmin;
            grid.buses[idx].Vg = g.Vg;
            grid.buses[idx].has_gen = true;
            if (grid.buses[idx].type == 2 || grid.buses[idx].type == 3) {
                grid.buses[idx].Vm = g.Vg;
            }
        }
    }

    // Identifikasi Reference Bus
    int ref_bus_idx = -1;
    for (int i = 0; i < n; ++i) {
        if (grid.buses[i].type == 3) {
            ref_bus_idx = i;
            grid.buses[i].Va = 0.0;
            break;
        }
    }
    if (ref_bus_idx == -1) {
        std::cerr << "Reference bus tidak ditemukan!\n";
        return;
    }

    // 3. Membangun Matriks Admittance Ybus = G + jB (Sparse Complex)
    auto t_ybus_start = Clock::now();
    std::vector<Eigen::Triplet<std::complex<double>>> y_triplets;
    y_triplets.reserve(grid.branches.size() * 4 + n);

    for (const auto& br : grid.branches) {
        if (br.status == 0) continue;

        auto it_f = id_to_idx.find(br.from);
        auto it_t = id_to_idx.find(br.to);
        if (it_f == id_to_idx.end() || it_t == id_to_idx.end()) continue;

        int f = it_f->second;
        int t = it_t->second;

        std::complex<double> zs(br.r, br.x);
        std::complex<double> ys = 1.0 / zs;
        double tap = (br.ratio == 0.0) ? 1.0 : br.ratio;
        double shift_rad = br.angle * M_PI / 180.0;
        std::complex<double> tap_complex = tap * std::exp(std::complex<double>(0.0, shift_rad));

        std::complex<double> Ytt = ys + std::complex<double>(0.0, br.b / 2.0);
        std::complex<double> Yff = Ytt / std::norm(tap_complex);
        std::complex<double> Yft = -ys / std::conj(tap_complex);
        std::complex<double> Ytf = -ys / tap_complex;

        y_triplets.emplace_back(f, f, Yff);
        y_triplets.emplace_back(t, t, Ytt);
        y_triplets.emplace_back(f, t, Yft);
        y_triplets.emplace_back(t, f, Ytf);
    }

    for (int i = 0; i < n; ++i) {
        std::complex<double> y_sh(grid.buses[i].Gs / grid.baseMVA, grid.buses[i].Bs / grid.baseMVA);
        y_triplets.emplace_back(i, i, y_sh);
    }

    Eigen::SparseMatrix<std::complex<double>> Ybus(n, n);
    Ybus.setFromTriplets(y_triplets.begin(), y_triplets.end());
    Ybus.makeCompressed();
    auto t_ybus_end = Clock::now();
    prof.ybus_us += std::chrono::duration<double, std::micro>(t_ybus_end - t_ybus_start).count();

    // 4. Inisialisasi Tegangan (Flat Start)
    std::vector<double> V(n);
    std::vector<double> Theta(n);
    for (int i = 0; i < n; ++i) {
        if (grid.buses[i].type == 1) {
            V[i] = 1.0;
        } else {
            V[i] = grid.buses[i].Vm;
        }
        Theta[i] = 0.0;
    }

    int q_lim_cycle = 0;
    const int max_q_lim_cycles = 10;
    int total_nr_iterations = 0;

    // Buffer Triplet yang Di-reuse (Menghindari Re-alokasi Heap per Iterasi)
    std::vector<Eigen::Triplet<double>> j_triplets;
    j_triplets.reserve(Ybus.nonZeros() * 4);

    while (true) {
        // Pemetaan variabel status aktif
        std::vector<int> pv_buses;
        std::vector<int> pq_buses;
        std::vector<int> non_ref_buses;
        std::unordered_map<int, int> theta_map;
        std::unordered_map<int, int> v_map;
        int theta_idx = 0;
        int v_idx = 0;

        for (int i = 0; i < n; ++i) {
            if (i == ref_bus_idx) continue;
            non_ref_buses.push_back(i);
            theta_map[i] = theta_idx++;
            if (grid.buses[i].type == 1) {
                pq_buses.push_back(i);
                v_map[i] = v_idx++;
            } else if (grid.buses[i].type == 2) {
                pv_buses.push_back(i);
            }
        }

        int n_theta = static_cast<int>(non_ref_buses.size());
        int n_v = static_cast<int>(pq_buses.size());
        int n_vars = n_theta + n_v;

        // Daya Spesifikasi P_spec & Q_spec
        std::vector<double> P_spec(n);
        std::vector<double> Q_spec(n);
        for (int i = 0; i < n; ++i) {
            P_spec[i] = (grid.buses[i].Pg - grid.buses[i].Pd) / grid.baseMVA;
            Q_spec[i] = (grid.buses[i].Qg - grid.buses[i].Qd) / grid.baseMVA;
        }

        // SOLVER INSTANCE UNTUK TOPOLOGI INI
        LinearSolver lu_solver;
        bool pattern_analyzed = false;

        bool converged = false;
        for (int iter = 1; iter <= max_iterations; ++iter) {
            total_nr_iterations++;

            // A. Evaluasi Injeksi Daya S_calc = V * conj(Ybus * V)
            auto t_inj_start = Clock::now();
            Eigen::VectorXcd Vc(n);
            for (int i = 0; i < n; ++i) {
                Vc(i) = std::polar(V[i], Theta[i]);
            }
            Eigen::VectorXcd Ic = Ybus * Vc;
            Eigen::VectorXcd Sc = Vc.cwiseProduct(Ic.conjugate());

            std::vector<double> P_calc(n);
            std::vector<double> Q_calc(n);
            for (int i = 0; i < n; ++i) {
                P_calc[i] = Sc(i).real();
                Q_calc[i] = Sc(i).imag();
            }
            auto t_inj_end = Clock::now();
            prof.power_inj_us += std::chrono::duration<double, std::micro>(t_inj_end - t_inj_start).count();

            // B. Evaluasi Mismatch dP dan dQ
            auto t_mis_start = Clock::now();
            Eigen::VectorXd dF(n_vars);
            double max_dP = 0.0;
            double max_dQ = 0.0;

            for (int idx : non_ref_buses) {
                int row = theta_map[idx];
                double dp = P_spec[idx] - P_calc[idx];
                dF(row) = dp;
                max_dP = std::max(max_dP, std::abs(dp));
            }

            for (int idx : pq_buses) {
                int row = n_theta + v_map[idx];
                double dq = Q_spec[idx] - Q_calc[idx];
                dF(row) = dq;
                max_dQ = std::max(max_dQ, std::abs(dq));
            }
            auto t_mis_end = Clock::now();
            prof.mismatch_us += std::chrono::duration<double, std::micro>(t_mis_end - t_mis_start).count();

            if (std::max(max_dP, max_dQ) < tolerance) {
                converged = true;
                break;
            }

            // C. Membangun Matriks Jacobian J (Sparse) dengan Re-using buffer triplets
            auto t_jac_start = Clock::now();
            j_triplets.clear();

            for (int i = 0; i < n; ++i) {
                for (Eigen::SparseMatrix<std::complex<double>>::InnerIterator it(Ybus, i); it; ++it) {
                    int j = static_cast<int>(it.row());
                    double Gij = it.value().real();
                    double Bij = it.value().imag();
                    double theta_ij = Theta[i] - Theta[j];

                    if (i != j) {
                        if (i != ref_bus_idx && j != ref_bus_idx) {
                            double val = V[i] * V[j] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
                            j_triplets.emplace_back(theta_map[i], theta_map[j], val);
                        }
                        if (i != ref_bus_idx && grid.buses[j].type == 1) {
                            double val = V[i] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
                            j_triplets.emplace_back(theta_map[i], n_theta + v_map[j], val);
                        }
                        if (grid.buses[i].type == 1 && j != ref_bus_idx) {
                            double val = -V[i] * V[j] * (Gij * std::cos(theta_ij) + Bij * std::sin(theta_ij));
                            j_triplets.emplace_back(n_theta + v_map[i], theta_map[j], val);
                        }
                        if (grid.buses[i].type == 1 && grid.buses[j].type == 1) {
                            double val = V[i] * (Gij * std::sin(theta_ij) - Bij * std::cos(theta_ij));
                            j_triplets.emplace_back(n_theta + v_map[i], n_theta + v_map[j], val);
                        }
                    } else {
                        if (i != ref_bus_idx) {
                            double val = -Q_calc[i] - Bij * V[i] * V[i];
                            j_triplets.emplace_back(theta_map[i], theta_map[i], val);
                        }
                        if (i != ref_bus_idx && grid.buses[i].type == 1) {
                            double val = P_calc[i] / V[i] + Gij * V[i];
                            j_triplets.emplace_back(theta_map[i], n_theta + v_map[j], val);
                        }
                        if (grid.buses[i].type == 1) {
                            double val = P_calc[i] - Gij * V[i] * V[i];
                            j_triplets.emplace_back(n_theta + v_map[i], theta_map[i], val);
                        }
                        if (grid.buses[i].type == 1) {
                            double val = Q_calc[i] / V[i] - Bij * V[i];
                            j_triplets.emplace_back(n_theta + v_map[i], n_theta + v_map[i], val);
                        }
                    }
                }
            }

            Eigen::SparseMatrix<double> J(n_vars, n_vars);
            J.setFromTriplets(j_triplets.begin(), j_triplets.end());
            J.makeCompressed();
            auto t_jac_end = Clock::now();
            prof.jac_assembly_us += std::chrono::duration<double, std::micro>(t_jac_end - t_jac_start).count();

            // D. OPTIMASI: ANALISIS POLA (SYMBOLIC) HANYA SEKALI
            if (!pattern_analyzed) {
                auto t_sym_start = Clock::now();
                lu_solver.analyzePattern(J);
                auto t_sym_end = Clock::now();
                prof.lu_symbolic_us += std::chrono::duration<double, std::micro>(t_sym_end - t_sym_start).count();
                pattern_analyzed = true;
            }

            // E. FAKTORISASI NUMERIK (FACTORIZE) SAJA DI SETIAP ITERASI
            auto t_num_start = Clock::now();
            lu_solver.factorize(J);
            auto t_num_end = Clock::now();
            prof.lu_numeric_us += std::chrono::duration<double, std::micro>(t_num_end - t_num_start).count();

            if (lu_solver.info() != Eigen::Success) {
                std::cerr << "Faktorisasi numerik SparseLU gagal!\n";
                return;
            }

            // F. Solve Linear System
            auto t_solve_start = Clock::now();
            Eigen::VectorXd dx = lu_solver.solve(dF);
            auto t_solve_end = Clock::now();
            prof.lu_solve_us += std::chrono::duration<double, std::micro>(t_solve_end - t_solve_start).count();

            if (lu_solver.info() != Eigen::Success) {
                std::cerr << "Penyelesaian Jacobian gagal!\n";
                return;
            }

            // G. Update Variabel Status
            auto t_upd_start = Clock::now();
            for (int idx : non_ref_buses) {
                Theta[idx] += dx(theta_map[idx]);
            }
            for (int idx : pq_buses) {
                V[idx] += dx(n_theta + v_map[idx]);
            }
            auto t_upd_end = Clock::now();
            prof.state_update_us += std::chrono::duration<double, std::micro>(t_upd_end - t_upd_start).count();
        }

        if (!converged) {
            std::cerr << "⚠️ Newton-Raphson tidak konvergen pada cycle " << q_lim_cycle << "!\n";
            return;
        }

        // H. Pemeriksaan Batas Reaktif Generator (Q-Limit Enforcement)
        if (!enforce_q_limits) {
            break;
        }

        auto t_qlim_start = Clock::now();
        Eigen::VectorXcd Vc(n);
        for (int i = 0; i < n; ++i) Vc(i) = std::polar(V[i], Theta[i]);
        Eigen::VectorXcd Ic = Ybus * Vc;
        Eigen::VectorXcd Sc = Vc.cwiseProduct(Ic.conjugate());

        bool violation_detected = false;
        for (int i = 0; i < n; ++i) {
            if (grid.buses[i].type == 2 && grid.buses[i].has_gen) {
                double Qg_needed = Sc(i).imag() * grid.baseMVA + grid.buses[i].Qd;
                if (Qg_needed > grid.buses[i].Qmax + 1e-4) {
                    grid.buses[i].type = 1;
                    grid.buses[i].Qg = grid.buses[i].Qmax;
                    grid.buses[i].q_limited = true;
                    violation_detected = true;
                } else if (Qg_needed < grid.buses[i].Qmin - 1e-4) {
                    grid.buses[i].type = 1;
                    grid.buses[i].Qg = grid.buses[i].Qmin;
                    grid.buses[i].q_limited = true;
                    violation_detected = true;
                }
            }
        }
        auto t_qlim_end = Clock::now();
        prof.qlim_check_us += std::chrono::duration<double, std::micro>(t_qlim_end - t_qlim_start).count();

        if (!violation_detected || ++q_lim_cycle >= max_q_lim_cycles) {
            break;
        }
    }

    // I. Post-Processing & Branch Flows
    auto t_post_start = Clock::now();
    Eigen::VectorXcd Vc(n);
    for (int i = 0; i < n; ++i) {
        Vc(i) = std::polar(V[i], Theta[i]);
        grid.buses[i].Vm = V[i];
        grid.buses[i].Va = Theta[i];
    }
    Eigen::VectorXcd Ic = Ybus * Vc;
    Eigen::VectorXcd Sc = Vc.cwiseProduct(Ic.conjugate());

    grid.buses[ref_bus_idx].Pg = Sc(ref_bus_idx).real() * grid.baseMVA + grid.buses[ref_bus_idx].Pd;
    grid.buses[ref_bus_idx].Qg = Sc(ref_bus_idx).imag() * grid.baseMVA + grid.buses[ref_bus_idx].Qd;

    for (int i = 0; i < n; ++i) {
        if (grid.buses[i].type == 2) {
            grid.buses[i].Qg = Sc(i).imag() * grid.baseMVA + grid.buses[i].Qd;
        }
    }

    double total_ploss_mw = 0.0;
    double total_qloss_mvar = 0.0;
    int overloaded_branches = 0;

    for (auto& br : grid.branches) {
        if (br.status == 0) continue;

        int f = id_to_idx[br.from];
        int t = id_to_idx[br.to];

        std::complex<double> zs(br.r, br.x);
        std::complex<double> ys = 1.0 / zs;
        double tap = (br.ratio == 0.0) ? 1.0 : br.ratio;
        double shift_rad = br.angle * M_PI / 180.0;
        std::complex<double> tap_complex = tap * std::exp(std::complex<double>(0.0, shift_rad));

        std::complex<double> Ytt = ys + std::complex<double>(0.0, br.b / 2.0);
        std::complex<double> Yff = Ytt / std::norm(tap_complex);
        std::complex<double> Yft = -ys / std::conj(tap_complex);
        std::complex<double> Ytf = -ys / tap_complex;

        std::complex<double> Vf = Vc(f);
        std::complex<double> Vt = Vc(t);

        std::complex<double> If = Yff * Vf + Yft * Vt;
        std::complex<double> It = Ytf * Vf + Ytt * Vt;

        std::complex<double> Sf = Vf * std::conj(If) * grid.baseMVA;
        std::complex<double> St = Vt * std::conj(It) * grid.baseMVA;

        br.P_from = Sf.real();
        br.Q_from = Sf.imag();
        br.P_to = St.real();
        br.Q_to = St.imag();

        br.Ploss = (Sf + St).real();
        br.Qloss = (Sf + St).imag();

        total_ploss_mw += br.Ploss;
        total_qloss_mvar += br.Qloss;

        br.S_max = std::max(std::abs(Sf), std::abs(St));
        if (br.rateA > 0.0) {
            br.loading_pct = (br.S_max / br.rateA) * 100.0;
            if (br.loading_pct > 100.0) {
                br.is_overloaded = true;
                overloaded_branches++;
            }
        }
    }
    auto t_post_end = Clock::now();
    prof.postproc_us += std::chrono::duration<double, std::micro>(t_post_end - t_post_start).count();

    auto t_end_total = Clock::now();
    prof.total_solve_us = std::chrono::duration<double, std::micro>(t_end_total - t_start_total).count();

    // Cetak Ringkasan Hasil
    std::cout << "\n================================================================================\n";
    std::cout << "|   Hasil Eksekusi AC Power Flow: " << solver_name << "\n";
    std::cout << "================================================================================\n";
    std::cout << "| Total Iterasi NR               : " << total_nr_iterations << " iterasi\n";
    std::cout << "| Total Siklus Q-Limit           : " << q_lim_cycle << " cycle\n";
    std::cout << "| Total Rugi Aktif (Ploss)       : " << std::fixed << std::setprecision(3) << total_ploss_mw << " MW\n";
    std::cout << "| Total Rugi Reaktif (Qloss)     : " << total_qloss_mvar << " MVAr\n";
    std::cout << "| Total Saluran Overload         : " << overloaded_branches << " cabang\n";
    std::cout << "| Waktu Eksekusi Solve           : " << std::setprecision(2) << prof.total_solve_us / 1000.0 << " ms\n";
    std::cout << "================================================================================\n";
}

void print_profiling_report(const ProfileMetrics& prof, const std::string& solver_name = "OPTIMIZED SOLVER") {
    struct Stage {
        std::string name;
        double us;
    };

    std::vector<Stage> stages = {
        {"1. Ybus Matrix Construction", prof.ybus_us},
        {"2. Power Injections (V*conj(Y*V))", prof.power_inj_us},
        {"3. Power Mismatch (dP, dQ)", prof.mismatch_us},
        {"4. Jacobian Triplet Assembly", prof.jac_assembly_us},
        {"5a. Symbolic Analysis (1x/cycle)", prof.lu_symbolic_us},
        {"5b. Numeric Factorize (per iter)", prof.lu_numeric_us},
        {"6. Linear Solve", prof.lu_solve_us},
        {"7. State Update (V, Theta)", prof.state_update_us},
        {"8. Q-Limit Checking & Clamping", prof.qlim_check_us},
        {"9. Post-Processing & Line Flows", prof.postproc_us}
    };

    std::vector<Stage> sorted_stages = stages;
    std::sort(sorted_stages.begin(), sorted_stages.end(), [](const Stage& a, const Stage& b) {
        return a.us > b.us;
    });

    double total_tracked_us = 0.0;
    for (const auto& s : stages) total_tracked_us += s.us;

    std::cout << "\n================================================================================\n";
    std::cout << "|                 PROFILING REPORT: " << solver_name << "\n";
    std::cout << "================================================================================\n";
    std::cout << "Stage Name                            Time (ms)    Time (us)     Share (%)   Profile Bar\n";
    std::cout << "-----------------------------------  -----------  -----------  ------------  -----------\n";

    for (const auto& s : sorted_stages) {
        double pct = (total_tracked_us > 0.0) ? (s.us / total_tracked_us * 100.0) : 0.0;
        int bar_len = static_cast<int>(pct / 4.0);
        std::string bar(bar_len, '#');

        printf("%-35s  %10.3f  %11.1f    %8.2f%%   %s\n",
               s.name.c_str(),
               s.us / 1000.0,
               s.us,
               pct,
               bar.c_str());
    }

    std::cout << "-----------------------------------  -----------  -----------  ------------  -----------\n";
    printf("%-35s  %10.3f  %11.1f     100.00%%\n", "TOTAL SOLVER TIME", total_tracked_us / 1000.0, total_tracked_us);
    std::cout << "================================================================================\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Penggunaan: " << argv[0] << " <matpower_case.m> [--no-qlim] [--klu | --sparselu | --compare]\n";
        return 1;
    }

    bool enforce_q_lims = true;
    bool use_klu = false;
    bool compare_both = false;

#ifdef HAVE_KLU
    use_klu = true; // default ke KLU jika tersedia
#endif

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-qlim") enforce_q_lims = false;
        else if (arg == "--klu") use_klu = true;
        else if (arg == "--sparselu") use_klu = false;
        else if (arg == "--compare") compare_both = true;
    }

    using Clock = std::chrono::high_resolution_clock;
    MatpowerACParser parser_base;
    auto t_pstart = Clock::now();
    if (!parser_base.load(argv[1])) {
        return 1;
    }
    auto t_pend = Clock::now();
    double parse_us = std::chrono::duration<double, std::micro>(t_pend - t_pstart).count();

    std::cout << "Berhasil memuat " << parser_base.buses.size() << " bus, " 
              << parser_base.generators.size() << " generator, dan "
              << parser_base.branches.size() << " cabang (Parsing I/O: " 
              << std::fixed << std::setprecision(2) << parse_us / 1000.0 << " ms).\n";

    if (compare_both) {
#ifdef HAVE_KLU
        std::cout << "\n>>> [1/2] Menjalankan dengan Eigen::SparseLU...\n";
        MatpowerACParser p1 = parser_base;
        ProfileMetrics prof_lu;
        run_ac_powerflow<Eigen::SparseLU<Eigen::SparseMatrix<double>>>(p1, enforce_q_lims, prof_lu, "Eigen::SparseLU");
        print_profiling_report(prof_lu, "Eigen::SparseLU");

        std::cout << "\n>>> [2/2] Menjalankan dengan Eigen::KLU (SuiteSparse)...\n";
        MatpowerACParser p2 = parser_base;
        ProfileMetrics prof_klu;
        run_ac_powerflow<Eigen::KLU<Eigen::SparseMatrix<double>>>(p2, enforce_q_lims, prof_klu, "Eigen::KLU");
        print_profiling_report(prof_klu, "Eigen::KLU");

        std::cout << "\n================================================================================\n";
        std::cout << "|             HEAD-TO-HEAD PERBANDINGAN: Eigen::SparseLU vs Eigen::KLU          |\n";
        std::cout << "================================================================================\n";
        printf("| %-38s : %10.2f ms\n", "Eigen::SparseLU Total Solve Time", prof_lu.total_solve_us / 1000.0);
        printf("| %-38s : %10.2f ms\n", "Eigen::KLU Total Solve Time", prof_klu.total_solve_us / 1000.0);
        printf("| %-38s : %10.2f ms\n", "  -> SparseLU Numeric Factorize", prof_lu.lu_numeric_us / 1000.0);
        printf("| %-38s : %10.2f ms\n", "  -> KLU Numeric Factorize", prof_klu.lu_numeric_us / 1000.0);
        double speedup = prof_lu.total_solve_us / prof_klu.total_solve_us;
        double fact_speedup = prof_lu.lu_numeric_us / prof_klu.lu_numeric_us;
        printf("| %-38s : %10.2fx LEBIH CEPAT!\n", "KLU Overall Speedup", speedup);
        printf("| %-38s : %10.2fx LEBIH CEPAT!\n", "KLU Numeric Factorize Speedup", fact_speedup);
        std::cout << "================================================================================\n";
#else
        std::cerr << "Peringatan: SuiteSparse KLU tidak terkompilasi! Menjalankan SparseLU saja.\n";
        ProfileMetrics prof_lu;
        run_ac_powerflow<Eigen::SparseLU<Eigen::SparseMatrix<double>>>(parser_base, enforce_q_lims, prof_lu, "Eigen::SparseLU");
        print_profiling_report(prof_lu, "Eigen::SparseLU");
#endif
    } else {
#ifdef HAVE_KLU
        if (use_klu) {
            ProfileMetrics prof_klu;
            run_ac_powerflow<Eigen::KLU<Eigen::SparseMatrix<double>>>(parser_base, enforce_q_lims, prof_klu, "Eigen::KLU");
            print_profiling_report(prof_klu, "Eigen::KLU");
        } else {
            ProfileMetrics prof_lu;
            run_ac_powerflow<Eigen::SparseLU<Eigen::SparseMatrix<double>>>(parser_base, enforce_q_lims, prof_lu, "Eigen::SparseLU");
            print_profiling_report(prof_lu, "Eigen::SparseLU");
        }
#else
        ProfileMetrics prof_lu;
        run_ac_powerflow<Eigen::SparseLU<Eigen::SparseMatrix<double>>>(parser_base, enforce_q_lims, prof_lu, "Eigen::SparseLU");
        print_profiling_report(prof_lu, "Eigen::SparseLU");
#endif
    }

    return 0;
}
