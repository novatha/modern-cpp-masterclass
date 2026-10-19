#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <complex>
#include <charconv>
#include <chrono>
#include <unordered_map>
#include <iomanip>
#include <algorithm>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

// Profiler Metrics untuk Fast Decoupled
struct IterStepTrace {
    int iter;
    char type; // 'P' or 'Q'
    double max_p_err;
    double max_q_err;
    double duration_us;
};

struct FDPFProfileMetrics {
    double parse_us{0.0};
    
    // One-Time Setup Phase
    double ybus_us{0.0};
    double bp_assembly_us{0.0};
    double bpp_assembly_us{0.0};
    double bp_cholesky_us{0.0};
    double bpp_cholesky_us{0.0};
    
    // Recurring Iteration Phase
    double p_mismatch_us{0.0};
    double q_mismatch_us{0.0};
    double bp_fwd_back_us{0.0}; // dVa solve
    double bpp_fwd_back_us{0.0}; // dVm solve
    double state_update_us{0.0};
    
    // Post-processing
    double postproc_us{0.0};
    double total_solve_us{0.0};

    int p_iters{0};
    int q_iters{0};
    std::vector<IterStepTrace> traces;
};

struct Bus {
    int id;
    int type; // 1 = PQ, 2 = PV, 3 = Ref
    double Pd{0.0}; // MW load
    double Qd{0.0}; // MVAr load
    double Gs{0.0}; // MW shunt at V = 1.0
    double Bs{0.0}; // MVAr shunt at V = 1.0
    double Vm{1.0}; // Voltage magnitude (p.u.)
    double Va{0.0}; // Voltage angle (rad)
    double Pg{0.0}; // MW gen
    double Qg{0.0}; // MVAr gen
    double Vg{1.0};
};

struct Branch {
    int from;
    int to;
    double r;
    double x;
    double b;
    double rateA;
    double ratio;
    double angle;
    int status;
    double Ploss{0.0};
    double Qloss{0.0};
    double S_max{0.0};
    double loading_pct{0.0};
    bool is_overloaded{false};
};

struct Generator {
    int bus;
    double Pg;
    double Qg;
    double Qmax;
    double Qmin;
    double Vg;
    int status;
};

class MatpowerFDPFParser {
public:
    double baseMVA{100.0};
    std::vector<Bus> buses;
    std::vector<Branch> branches;
    std::vector<Generator> generators;

    bool load(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Gagal membuka berkas: " << filepath << "\n";
            return false;
        }

        std::string line;
        enum class Section { None, Bus, Gen, Branch };
        Section current_section = Section::None;

        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || line[start] == '%') continue;

            if (line.find("mpc.baseMVA") != std::string::npos) {
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos) {
                    size_t sc_pos = line.find(';', eq_pos);
                    std::string val_str = line.substr(eq_pos + 1, sc_pos - eq_pos - 1);
                    baseMVA = std::stod(val_str);
                }
                continue;
            }

            if (line.find("mpc.bus") != std::string::npos && line.find('[') != std::string::npos) {
                current_section = Section::Bus;
                continue;
            } else if (line.find("mpc.gen") != std::string::npos && line.find('[') != std::string::npos) {
                current_section = Section::Gen;
                continue;
            } else if (line.find("mpc.branch") != std::string::npos && line.find('[') != std::string::npos) {
                current_section = Section::Branch;
                continue;
            }

            if (line.find("];") != std::string::npos) {
                current_section = Section::None;
                continue;
            }

            if (current_section == Section::None) continue;

            std::vector<std::string_view> tokens;
            std::string_view sv = line;
            size_t pos = 0;
            while (pos < sv.size()) {
                while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == ';')) pos++;
                if (pos >= sv.size() || sv[pos] == '%') break;
                size_t token_start = pos;
                while (pos < sv.size() && sv[pos] != ' ' && sv[pos] != '\t' && sv[pos] != ';' && sv[pos] != '%') pos++;
                tokens.push_back(sv.substr(token_start, pos - token_start));
            }

            if (tokens.empty()) continue;

            auto parse_double = [](std::string_view s) -> double {
                double val = 0.0;
                std::from_chars(s.data(), s.data() + s.size(), val);
                return val;
            };
            auto parse_int = [](std::string_view s) -> int {
                int val = 0;
                std::from_chars(s.data(), s.data() + s.size(), val);
                return val;
            };

            if (current_section == Section::Bus && tokens.size() >= 10) {
                Bus b;
                b.id = parse_int(tokens[0]);
                b.type = parse_int(tokens[1]);
                b.Pd = parse_double(tokens[2]);
                b.Qd = parse_double(tokens[3]);
                b.Gs = parse_double(tokens[4]);
                b.Bs = parse_double(tokens[5]);
                b.Vm = parse_double(tokens[7]);
                b.Va = parse_double(tokens[8]) * M_PI / 180.0;
                buses.push_back(b);
            } else if (current_section == Section::Gen && tokens.size() >= 8) {
                Generator g;
                g.bus = parse_int(tokens[0]);
                g.Pg = parse_double(tokens[1]);
                g.Qg = parse_double(tokens[2]);
                g.Qmax = parse_double(tokens[3]);
                g.Qmin = parse_double(tokens[4]);
                g.Vg = parse_double(tokens[5]);
                g.status = parse_int(tokens[7]);
                if (g.status > 0) generators.push_back(g);
            } else if (current_section == Section::Branch && tokens.size() >= 11) {
                Branch br;
                br.from = parse_int(tokens[0]);
                br.to = parse_int(tokens[1]);
                br.r = parse_double(tokens[2]);
                br.x = parse_double(tokens[3]);
                br.b = parse_double(tokens[4]);
                br.rateA = parse_double(tokens[5]);
                br.ratio = parse_double(tokens[8]);
                if (br.ratio == 0.0) br.ratio = 1.0;
                br.angle = parse_double(tokens[9]) * M_PI / 180.0;
                br.status = parse_int(tokens[10]);
                branches.push_back(br);
            }
        }
        return true;
    }
};

void run_fast_decoupled_xb(MatpowerFDPFParser& grid, FDPFProfileMetrics& prof, double tol = 1e-6, int max_iterations = 30) {
    using Clock = std::chrono::high_resolution_clock;
    int n = static_cast<int>(grid.buses.size());
    auto t_start_total = Clock::now();

    // 1. Mapping Bus ID -> Index
    std::unordered_map<int, int> id_to_idx;
    for (int i = 0; i < n; ++i) {
        id_to_idx[grid.buses[i].id] = i;
    }

    // Hubungkan generator ke bus
    for (const auto& g : grid.generators) {
        auto it = id_to_idx.find(g.bus);
        if (it != id_to_idx.end()) {
            int idx = it->second;
            grid.buses[idx].Pg += g.Pg;
            grid.buses[idx].Qg += g.Qg;
            grid.buses[idx].Vg = g.Vg;
            if (grid.buses[idx].type == 2 || grid.buses[idx].type == 3) {
                grid.buses[idx].Vm = g.Vg;
            }
        }
    }

    int ref_bus_idx = -1;
    for (int i = 0; i < n; ++i) {
        if (grid.buses[i].type == 3) {
            ref_bus_idx = i;
            grid.buses[i].Va = 0.0;
            break;
        }
    }

    // Pemetaan non-ref dan PQ
    std::vector<int> non_ref_buses;
    std::vector<int> pq_buses;
    std::unordered_map<int, int> non_ref_map;
    std::unordered_map<int, int> pq_map;
    int nr_idx = 0;
    int pq_idx = 0;

    for (int i = 0; i < n; ++i) {
        if (i != ref_bus_idx) {
            non_ref_buses.push_back(i);
            non_ref_map[i] = nr_idx++;
        }
        if (grid.buses[i].type == 1) {
            pq_buses.push_back(i);
            pq_map[i] = pq_idx++;
        }
    }

    int n_nr = static_cast<int>(non_ref_buses.size());
    int n_pq = static_cast<int>(pq_buses.size());

    // 2. Bentuk Matriks Ybus Lengkap (untuk evaluasi daya dan mismatch)
    auto t_ybus_start = Clock::now();
    std::vector<Eigen::Triplet<std::complex<double>>> y_triplets;
    y_triplets.reserve(grid.branches.size() * 4 + n);

    for (const auto& br : grid.branches) {
        if (br.status == 0) continue;
        int f = id_to_idx[br.from];
        int t = id_to_idx[br.to];

        std::complex<double> z(br.r, br.x);
        std::complex<double> y = 1.0 / z;
        double a = br.ratio;
        double phi = br.angle;
        std::complex<double> tap = std::polar(a, phi);
        std::complex<double> b_sh(0.0, br.b / 2.0);

        std::complex<double> Yff = (y + b_sh) / (a * a);
        std::complex<double> Ytt = y + b_sh;
        std::complex<double> Yft = -y / std::conj(tap);
        std::complex<double> Ytf = -y / tap;

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

    // 3. Bentuk Matriks B' (Bp) dan B'' (Bpp) untuk Fast Decoupled XB Model
    // A. Matriks B'
    auto t_bp_start = Clock::now();
    std::vector<Eigen::Triplet<double>> bp_triplets;
    bp_triplets.reserve(grid.branches.size() * 4);

    for (const auto& br : grid.branches) {
        if (br.status == 0) continue;
        int f = id_to_idx[br.from];
        int t = id_to_idx[br.to];

        double b_series = -1.0 / br.x;
        double off_diag = b_series; 
        double diag_contrib = -b_series;

        if (f != ref_bus_idx && t != ref_bus_idx) {
            bp_triplets.emplace_back(non_ref_map[f], non_ref_map[t], off_diag);
            bp_triplets.emplace_back(non_ref_map[t], non_ref_map[f], off_diag);
        }
        if (f != ref_bus_idx) {
            bp_triplets.emplace_back(non_ref_map[f], non_ref_map[f], diag_contrib);
        }
        if (t != ref_bus_idx) {
            bp_triplets.emplace_back(non_ref_map[t], non_ref_map[t], diag_contrib);
        }
    }

    Eigen::SparseMatrix<double> Bp(n_nr, n_nr);
    Bp.setFromTriplets(bp_triplets.begin(), bp_triplets.end());
    Bp.makeCompressed();
    auto t_bp_end = Clock::now();
    prof.bp_assembly_us = std::chrono::duration<double, std::micro>(t_bp_end - t_bp_start).count();

    // B. Matriks B''
    auto t_bpp_start = Clock::now();
    std::vector<Eigen::Triplet<double>> bpp_triplets;
    bpp_triplets.reserve(grid.branches.size() * 4 + n_pq);

    for (const auto& br : grid.branches) {
        if (br.status == 0) continue;
        int f = id_to_idx[br.from];
        int t = id_to_idx[br.to];

        double r2_x2 = br.r * br.r + br.x * br.x;
        double b_val = -br.x / r2_x2;
        double b_sh = br.b / 2.0;
        double a = br.ratio;

        double bpp_ff = -(b_val + b_sh) / (a * a);
        double bpp_tt = -(b_val + b_sh);
        double bpp_ft = b_val / a;

        bool f_is_pq = (grid.buses[f].type == 1);
        bool t_is_pq = (grid.buses[t].type == 1);

        if (f_is_pq && t_is_pq) {
            bpp_triplets.emplace_back(pq_map[f], pq_map[t], bpp_ft);
            bpp_triplets.emplace_back(pq_map[t], pq_map[f], bpp_ft);
        }
        if (f_is_pq) {
            bpp_triplets.emplace_back(pq_map[f], pq_map[f], bpp_ff);
        }
        if (t_is_pq) {
            bpp_triplets.emplace_back(pq_map[t], pq_map[t], bpp_tt);
        }
    }

    for (int idx : pq_buses) {
        double b_shunt = -grid.buses[idx].Bs / grid.baseMVA;
        bpp_triplets.emplace_back(pq_map[idx], pq_map[idx], b_shunt);
    }

    Eigen::SparseMatrix<double> Bpp(n_pq, n_pq);
    Bpp.setFromTriplets(bpp_triplets.begin(), bpp_triplets.end());
    Bpp.makeCompressed();
    auto t_bpp_end = Clock::now();
    prof.bpp_assembly_us = std::chrono::duration<double, std::micro>(t_bpp_end - t_bpp_start).count();

    // 4. FACTORIZE MATRIKS Bp DAN Bpp DENGAN CHOLESKY (SIMPLICIAL LDLT)
    auto t_fact_bp_start = Clock::now();
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver_Bp;
    solver_Bp.compute(Bp);
    if (solver_Bp.info() != Eigen::Success) {
        std::cerr << "Faktorisasi Cholesky B' gagal!\n";
        return;
    }
    auto t_fact_bp_end = Clock::now();
    prof.bp_cholesky_us = std::chrono::duration<double, std::micro>(t_fact_bp_end - t_fact_bp_start).count();

    auto t_fact_bpp_start = Clock::now();
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver_Bpp;
    solver_Bpp.compute(Bpp);
    if (solver_Bpp.info() != Eigen::Success) {
        std::cerr << "Faktorisasi Cholesky B'' gagal!\n";
        return;
    }
    auto t_fact_bpp_end = Clock::now();
    prof.bpp_cholesky_us = std::chrono::duration<double, std::micro>(t_fact_bpp_end - t_fact_bpp_start).count();

    // 5. Inisialisasi Tegangan (Flat Start)
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

    std::vector<double> P_spec(n);
    std::vector<double> Q_spec(n);
    for (int i = 0; i < n; ++i) {
        P_spec[i] = (grid.buses[i].Pg - grid.buses[i].Pd) / grid.baseMVA;
        Q_spec[i] = (grid.buses[i].Qg - grid.buses[i].Qd) / grid.baseMVA;
    }

    // 6. Fast Decoupled Iteration Loop (P-Theta & Q-V Alternating)
    bool converged = false;
    int p_iters = 0;
    int q_iters = 0;

    Eigen::VectorXd dVa(n_nr);
    Eigen::VectorXd dVm(n_pq);

    for (int iter = 1; iter <= max_iterations; ++iter) {
        // --- STEP P-ITERATION: Update Sudut Fasa (Theta) ---
        auto t_p_step_start = Clock::now();
        auto t_pmis_start = Clock::now();
        Eigen::VectorXcd Vc(n);
        for (int i = 0; i < n; ++i) Vc(i) = std::polar(V[i], Theta[i]);
        Eigen::VectorXcd Ic = Ybus * Vc;
        Eigen::VectorXcd Sc = Vc.cwiseProduct(Ic.conjugate());

        Eigen::VectorXd mis_P(n_nr);
        double max_p_mis = 0.0;
        for (int i = 0; i < n_nr; ++i) {
            int bus_idx = non_ref_buses[i];
            double p_calc = Sc(bus_idx).real();
            double p_err = (p_calc - P_spec[bus_idx]) / V[bus_idx];
            mis_P(i) = p_err;
            max_p_mis = std::max(max_p_mis, std::abs(p_err));
        }

        double max_q_mis = 0.0;
        for (int i = 0; i < n_pq; ++i) {
            int bus_idx = pq_buses[i];
            double q_calc = Sc(bus_idx).imag();
            double q_err = (q_calc - Q_spec[bus_idx]) / V[bus_idx];
            max_q_mis = std::max(max_q_mis, std::abs(q_err));
        }
        auto t_pmis_end = Clock::now();
        prof.p_mismatch_us += std::chrono::duration<double, std::micro>(t_pmis_end - t_pmis_start).count();

        if (max_p_mis < tol && max_q_mis < tol) {
            converged = true;
            break;
        }

        // P-Step Solve: Bp * dVa = -mis_P (Murni Forward & Backward Substitution)
        auto t_psolve_start = Clock::now();
        dVa = solver_Bp.solve(-mis_P);
        auto t_psolve_end = Clock::now();
        prof.bp_fwd_back_us += std::chrono::duration<double, std::micro>(t_psolve_end - t_psolve_start).count();

        auto t_pupd_start = Clock::now();
        for (int i = 0; i < n_nr; ++i) {
            Theta[non_ref_buses[i]] += dVa(i);
        }
        auto t_pupd_end = Clock::now();
        prof.state_update_us += std::chrono::duration<double, std::micro>(t_pupd_end - t_pupd_start).count();
        p_iters++;

        auto t_p_step_end = Clock::now();
        double p_step_us = std::chrono::duration<double, std::micro>(t_p_step_end - t_p_step_start).count();
        prof.traces.push_back({iter, 'P', max_p_mis, max_q_mis, p_step_us});

        // --- STEP Q-ITERATION: Update Magnitudo Tegangan (Vm) ---
        auto t_q_step_start = Clock::now();
        auto t_qmis_start = Clock::now();
        for (int i = 0; i < n; ++i) Vc(i) = std::polar(V[i], Theta[i]);
        Ic = Ybus * Vc;
        Sc = Vc.cwiseProduct(Ic.conjugate());

        Eigen::VectorXd mis_Q(n_pq);
        max_q_mis = 0.0;
        for (int i = 0; i < n_pq; ++i) {
            int bus_idx = pq_buses[i];
            double q_calc = Sc(bus_idx).imag();
            double q_err = (q_calc - Q_spec[bus_idx]) / V[bus_idx];
            mis_Q(i) = q_err;
            max_q_mis = std::max(max_q_mis, std::abs(q_err));
        }

        max_p_mis = 0.0;
        for (int i = 0; i < n_nr; ++i) {
            int bus_idx = non_ref_buses[i];
            double p_calc = Sc(bus_idx).real();
            double p_err = (p_calc - P_spec[bus_idx]) / V[bus_idx];
            max_p_mis = std::max(max_p_mis, std::abs(p_err));
        }
        auto t_qmis_end = Clock::now();
        prof.q_mismatch_us += std::chrono::duration<double, std::micro>(t_qmis_end - t_qmis_start).count();

        if (max_p_mis < tol && max_q_mis < tol) {
            converged = true;
            break;
        }

        // Q-Step Solve: Bpp * dVm = -mis_Q (Murni Forward & Backward Substitution)
        auto t_qsolve_start = Clock::now();
        dVm = solver_Bpp.solve(-mis_Q);
        auto t_qsolve_end = Clock::now();
        prof.bpp_fwd_back_us += std::chrono::duration<double, std::micro>(t_qsolve_end - t_qsolve_start).count();

        t_pupd_start = Clock::now();
        for (int i = 0; i < n_pq; ++i) {
            V[pq_buses[i]] += dVm(i);
        }
        t_pupd_end = Clock::now();
        prof.state_update_us += std::chrono::duration<double, std::micro>(t_pupd_end - t_pupd_start).count();
        q_iters++;

        auto t_q_step_end = Clock::now();
        double q_step_us = std::chrono::duration<double, std::micro>(t_q_step_end - t_q_step_start).count();
        prof.traces.push_back({iter, 'Q', max_p_mis, max_q_mis, q_step_us});
    }

    prof.p_iters = p_iters;
    prof.q_iters = q_iters;

    if (!converged) {
        std::cerr << "⚠️ Fast Decoupled Power Flow tidak konvergen dalam batas iterasi!\n";
        return;
    }

    // 7. Post-Processing & Perhitungan Aliran Daya Cabang
    auto t_post_start = Clock::now();
    Eigen::VectorXcd V_final(n);
    for (int i = 0; i < n; ++i) {
        V_final(i) = std::polar(V[i], Theta[i]);
        grid.buses[i].Vm = V[i];
        grid.buses[i].Va = Theta[i];
    }

    Eigen::VectorXcd I_final = Ybus * V_final;
    Eigen::VectorXcd S_final = V_final.cwiseProduct(I_final.conjugate());

    grid.buses[ref_bus_idx].Pg = S_final(ref_bus_idx).real() * grid.baseMVA + grid.buses[ref_bus_idx].Pd;
    grid.buses[ref_bus_idx].Qg = S_final(ref_bus_idx).imag() * grid.baseMVA + grid.buses[ref_bus_idx].Qd;

    for (int i = 0; i < n; ++i) {
        if (grid.buses[i].type == 2) {
            grid.buses[i].Qg = S_final(i).imag() * grid.baseMVA + grid.buses[i].Qd;
        }
    }

    double total_ploss_mw = 0.0;
    double total_qloss_mvar = 0.0;
    int overloaded_branches = 0;

    for (auto& br : grid.branches) {
        if (br.status == 0) continue;
        int f = id_to_idx[br.from];
        int t = id_to_idx[br.to];

        std::complex<double> z(br.r, br.x);
        std::complex<double> y = 1.0 / z;
        double a = br.ratio;
        double phi = br.angle;
        std::complex<double> tap = std::polar(a, phi);
        std::complex<double> b_sh(0.0, br.b / 2.0);

        std::complex<double> Vf = V_final(f);
        std::complex<double> Vt = V_final(t);

        std::complex<double> If = (Vf / tap - Vt) * y / std::conj(tap) + Vf * (b_sh / (a * a));
        std::complex<double> It = (Vt - Vf / tap) * y + Vt * b_sh;

        std::complex<double> Sf = Vf * std::conj(If) * grid.baseMVA;
        std::complex<double> St = Vt * std::conj(It) * grid.baseMVA;

        std::complex<double> Sloss = Sf + St;
        br.Ploss = Sloss.real();
        br.Qloss = Sloss.imag();
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

    // Cetak Ringkasan
    std::cout << "\n================================================================================\n";
    std::cout << "|   Hasil Eksekusi Fast Decoupled Power Flow (FDXB - Cholesky SimplicialLDLT) |\n";
    std::cout << "================================================================================\n";
    std::cout << "| Total Iterasi P (P-theta)      : " << p_iters << " iterasi\n";
    std::cout << "| Total Iterasi Q (Q-V)          : " << q_iters << " iterasi\n";
    std::cout << "| Total Rugi Aktif (Ploss)       : " << std::fixed << std::setprecision(3) << total_ploss_mw << " MW\n";
    std::cout << "| Total Rugi Reaktif (Qloss)     : " << total_qloss_mvar << " MVAr\n";
    std::cout << "| Total Saluran Overload         : " << overloaded_branches << " cabang\n";
    std::cout << "| Waktu Eksekusi Solve           : " << std::setprecision(2) << prof.total_solve_us / 1000.0 << " ms\n";
    std::cout << "================================================================================\n";
}

void print_fdpf_report(const FDPFProfileMetrics& prof) {
    // 1. Trace Konvergensi Per Iterasi
    std::cout << "\n================================================================================\n";
    std::cout << "|                TRACE ITERASI & KONVERGENSI (RESIDUAL HISTORY)                |\n";
    std::cout << "================================================================================\n";
    std::cout << "Iter   Step      Max P Mismatch (p.u.)    Max Q Mismatch (p.u.)    Step Time (us)\n";
    std::cout << "----   ----      ---------------------    ---------------------    --------------\n";
    for (const auto& tr : prof.traces) {
        printf(" %2d     %c         %15.3e          %15.3e          %10.1f\n",
               tr.iter, tr.type, tr.max_p_err, tr.max_q_err, tr.duration_us);
    }
    std::cout << "================================================================================\n";

    // 2. Breakdown Dua Fase (Setup vs Recurring Iterations)
    double setup_us = prof.ybus_us + prof.bp_assembly_us + prof.bpp_assembly_us + prof.bp_cholesky_us + prof.bpp_cholesky_us;
    double iter_us = prof.p_mismatch_us + prof.q_mismatch_us + prof.bp_fwd_back_us + prof.bpp_fwd_back_us + prof.state_update_us;
    double total_us = setup_us + iter_us + prof.postproc_us;

    std::cout << "\n================================================================================\n";
    std::cout << "|              PROFILING KOMPREHENSIF: DUA FASE (SETUP vs ITERASI)             |\n";
    std::cout << "================================================================================\n";
    std::cout << "Komponen Komputasi                    Time (ms)    Time (us)     Share (%)   Status\n";
    std::cout << "-----------------------------------  -----------  -----------  ------------  -----------\n";
    
    std::cout << "[FASE 1: SETUP TOPOLOGI & CHOLESKY (HANYA 1X DILAKUKAN)]\n";
    printf("  1a. Matriks Ybus Kompleks           %10.3f  %11.1f    %8.2f%%   1x setup\n", prof.ybus_us / 1000.0, prof.ybus_us, prof.ybus_us / total_us * 100.0);
    printf("  1b. Assembly Matriks B' (Bp)        %10.3f  %11.1f    %8.2f%%   1x setup\n", prof.bp_assembly_us / 1000.0, prof.bp_assembly_us, prof.bp_assembly_us / total_us * 100.0);
    printf("  1c. Assembly Matriks B'' (Bpp)      %10.3f  %11.1f    %8.2f%%   1x setup\n", prof.bpp_assembly_us / 1000.0, prof.bpp_assembly_us, prof.bpp_assembly_us / total_us * 100.0);
    printf("  1d. Cholesky Factorize B' (LDLT)    %10.3f  %11.1f    %8.2f%%   1x setup\n", prof.bp_cholesky_us / 1000.0, prof.bp_cholesky_us, prof.bp_cholesky_us / total_us * 100.0);
    printf("  1e. Cholesky Factorize B'' (LDLT)   %10.3f  %11.1f    %8.2f%%   1x setup\n", prof.bpp_cholesky_us / 1000.0, prof.bpp_cholesky_us, prof.bpp_cholesky_us / total_us * 100.0);
    printf("  >>> Subtotal Setup Phase            %10.3f  %11.1f    %8.2f%%\n", setup_us / 1000.0, setup_us, setup_us / total_us * 100.0);

    std::cout << "\n[FASE 2: RECURRING ITERATION LOOP (BIAYA PER SOLVE PADA BATCH N-k)]\n";
    printf("  2a. Evaluasi Mismatch P (Sc.real)   %10.3f  %11.1f    %8.2f%%   %d iter\n", prof.p_mismatch_us / 1000.0, prof.p_mismatch_us, prof.p_mismatch_us / total_us * 100.0, prof.p_iters);
    printf("  2b. Evaluasi Mismatch Q (Sc.imag)   %10.3f  %11.1f    %8.2f%%   %d iter\n", prof.q_mismatch_us / 1000.0, prof.q_mismatch_us, prof.q_mismatch_us / total_us * 100.0, prof.q_iters);
    printf("  2c. B' Fwd/Back Solve (dVa)         %10.3f  %11.1f    %8.2f%%   %d iter (%.1fus/it)\n", prof.bp_fwd_back_us / 1000.0, prof.bp_fwd_back_us, prof.bp_fwd_back_us / total_us * 100.0, prof.p_iters, prof.bp_fwd_back_us / std::max(1, prof.p_iters));
    printf("  2d. B'' Fwd/Back Solve (dVm)        %10.3f  %11.1f    %8.2f%%   %d iter (%.1fus/it)\n", prof.bpp_fwd_back_us / 1000.0, prof.bpp_fwd_back_us, prof.bpp_fwd_back_us / total_us * 100.0, prof.q_iters, prof.bpp_fwd_back_us / std::max(1, prof.q_iters));
    printf("  2e. State Updates (Theta & Vm)      %10.3f  %11.1f    %8.2f%%\n", prof.state_update_us / 1000.0, prof.state_update_us, prof.state_update_us / total_us * 100.0);
    printf("  >>> Subtotal Iteration Phase        %10.3f  %11.1f    %8.2f%%\n", iter_us / 1000.0, iter_us, iter_us / total_us * 100.0);

    std::cout << "\n[FASE 3: POST-PROCESSING]\n";
    printf("  3a. Aliran Saluran & Ploss          %10.3f  %11.1f    %8.2f%%\n", prof.postproc_us / 1000.0, prof.postproc_us, prof.postproc_us / total_us * 100.0);
    std::cout << "-----------------------------------  -----------  -----------  ------------  -----------\n";
    printf("%-35s  %10.3f  %11.1f     100.00%%\n", "TOTAL WAKTU (Cold Start)", total_us / 1000.0, total_us);
    std::cout << "================================================================================\n";

    double batch_solve_ms = (iter_us + prof.postproc_us) / 1000.0;
    double throughput = (batch_solve_ms > 0) ? (1000.0 / batch_solve_ms) : 0;
    std::cout << "💡 ANALISIS BATCH & KONTINGENSI N-k:\n";
    printf("   - Jika matriks B' dan B'' telah di-setup/difaktorkan sebelumnya di awal:\n");
    printf("   - Biaya per aliran daya hanyalah Fase Iterasi: %.3f ms\n", batch_solve_ms);
    printf("   - Throughput estimasi: ~%.0f ALIRAN DAYA PER DETIK per CPU core!\n", throughput);
    std::cout << "================================================================================\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Penggunaan: " << argv[0] << " <matpower_case.m>\n";
        return 1;
    }

    using Clock = std::chrono::high_resolution_clock;
    MatpowerFDPFParser parser;
    auto t_pstart = Clock::now();
    if (!parser.load(argv[1])) {
        return 1;
    }
    auto t_pend = Clock::now();
    double parse_us = std::chrono::duration<double, std::micro>(t_pend - t_pstart).count();

    std::cout << "Berhasil memuat " << parser.buses.size() << " bus, " 
              << parser.generators.size() << " generator, dan "
              << parser.branches.size() << " cabang (Parsing I/O: " 
              << std::fixed << std::setprecision(2) << parse_us / 1000.0 << " ms).\n";

    FDPFProfileMetrics prof;
    run_fast_decoupled_xb(parser, prof);
    print_fdpf_report(prof);

    return 0;
}
