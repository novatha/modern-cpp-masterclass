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
#include <thread>
#include <atomic>
#include <numeric>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#ifdef HAVE_KLU
#include <Eigen/KLUSupport>
#endif

// ============================================================================
// STRUKTUR DATA POWER GRID & KONTINGENSI
// ============================================================================
struct Bus {
    int id{0};
    int type{1}; // 1 = PQ, 2 = PV, 3 = Ref
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
    int id{0};
    int from{0};
    int to{0};
    double r{0.0};
    double x{0.0};
    double b{0.0};
    double rateA{0.0};
    double ratio{1.0};
    double angle{0.0};
    int status{1}; // 1 = in-service, 0 = out-of-service
};

struct Generator {
    int bus{0};
    double Pg{0.0};
    double Qg{0.0};
    double Qmax{0.0};
    double Qmin{0.0};
    double Vg{1.0};
    int status{1};
};

enum class ContingencyStatus {
    Converged,
    Islanding,
    Diverged
};

struct ContingencyResult {
    int branch_idx{-1};
    int from_bus{0};
    int to_bus{0};
    ContingencyStatus status{ContingencyStatus::Diverged};
    int p_iters{0};
    int q_iters{0};
    double solve_time_us{0.0};
    
    // Severity & Metrics
    double max_loading_pct{0.0};
    int overloaded_branch_count{0};
    double min_voltage{1.0};
    int min_voltage_bus{0};
    double max_voltage{1.0};
    int max_voltage_bus{0};
    int voltage_violation_count{0};
    
    double PI_V{0.0};      // Voltage Performance Index
    double PI_P{0.0};      // Thermal Overload Performance Index
    double PI_Total{0.0};  // Composite Severity Index
    double Ploss_mw{0.0};
    
    // Islanding Details
    double isolated_load_mw{0.0};
    int isolated_bus_count{0};
};

// ============================================================================
// MATPOWER CASE PARSER
// ============================================================================
class GridModel {
public:
    double baseMVA{100.0};
    std::vector<Bus> buses;
    std::vector<Branch> branches;
    std::vector<Generator> generators;
    std::unordered_map<int, int> id_to_idx;
    int ref_bus_idx{-1};
    double total_load_mw{0.0};
    double total_gen_mw{0.0};

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
                br.id = static_cast<int>(branches.size()) + 1;
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

        int n = static_cast<int>(buses.size());
        for (int i = 0; i < n; ++i) {
            id_to_idx[buses[i].id] = i;
            total_load_mw += buses[i].Pd;
        }

        for (const auto& g : generators) {
            auto it = id_to_idx.find(g.bus);
            if (it != id_to_idx.end()) {
                int idx = it->second;
                buses[idx].Pg += g.Pg;
                buses[idx].Qg += g.Qg;
                buses[idx].Vg = g.Vg;
                if (buses[idx].type == 2 || buses[idx].type == 3) {
                    buses[idx].Vm = g.Vg;
                }
                total_gen_mw += g.Pg;
            }
        }

        for (int i = 0; i < n; ++i) {
            if (buses[i].type == 3) {
                ref_bus_idx = i;
                buses[i].Va = 0.0;
                break;
            }
        }

        return true;
    }
};

// ============================================================================
// DISJOINT SET UNION (DSU) UNTUK DETEKSI ISLANDING
// ============================================================================
struct DSU {
    std::vector<int> parent;
    DSU(int n) : parent(n) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int i) {
        if (parent[i] == i) return i;
        return parent[i] = find(parent[i]);
    }
    bool unite(int i, int j) {
        int root_i = find(i);
        int root_j = find(j);
        if (root_i != root_j) {
            parent[root_i] = root_j;
            return true;
        }
        return false;
    }
};

// ============================================================================
// SOLVER WORKSPACE (THREAD-LOCAL TANPA LOCKING)
// ============================================================================
struct SolverWorkspace {
    std::vector<double> V;
    std::vector<double> Theta;
    std::vector<double> P_spec;
    std::vector<double> Q_spec;
    std::vector<int> non_ref_buses;
    std::vector<int> pq_buses;
    std::unordered_map<int, int> non_ref_map;
    std::unordered_map<int, int> pq_map;
    
    // Solvers
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver_Bp;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver_Bpp;

    void init(const GridModel& grid) {
        int n = static_cast<int>(grid.buses.size());
        V.resize(n);
        Theta.resize(n);
        P_spec.resize(n);
        Q_spec.resize(n);

        non_ref_buses.clear();
        pq_buses.clear();
        non_ref_map.clear();
        pq_map.clear();

        int nr_idx = 0;
        int pq_idx = 0;
        for (int i = 0; i < n; ++i) {
            if (i != grid.ref_bus_idx) {
                non_ref_buses.push_back(i);
                non_ref_map[i] = nr_idx++;
            }
            if (grid.buses[i].type == 1) {
                pq_buses.push_back(i);
                pq_map[i] = pq_idx++;
            }
            P_spec[i] = (grid.buses[i].Pg - grid.buses[i].Pd) / grid.baseMVA;
            Q_spec[i] = (grid.buses[i].Qg - grid.buses[i].Qd) / grid.baseMVA;
        }
    }
};

// ============================================================================
// SIMULASI FAST DECOUPLED UNTUK 1 KONTINGENSI
// ============================================================================
ContingencyResult simulate_single_fdxb(
    const GridModel& grid,
    int tripped_branch_idx,
    SolverWorkspace& ws,
    const std::vector<double>& warm_V,
    const std::vector<double>& warm_Theta,
    double tol = 1e-4,
    int max_iters = 30)
{
    using Clock = std::chrono::high_resolution_clock;
    auto t_start = Clock::now();

    ContingencyResult res;
    res.branch_idx = tripped_branch_idx;
    res.from_bus = grid.branches[tripped_branch_idx].from;
    res.to_bus = grid.branches[tripped_branch_idx].to;

    int n = static_cast<int>(grid.buses.size());
    int n_nr = static_cast<int>(ws.non_ref_buses.size());
    int n_pq = static_cast<int>(ws.pq_buses.size());

    // 1. CEK KONEKTIVITAS & ISLANDING DENGAN DSU
    DSU dsu(n);
    for (size_t i = 0; i < grid.branches.size(); ++i) {
        if (static_cast<int>(i) == tripped_branch_idx || grid.branches[i].status == 0) continue;
        int f = grid.id_to_idx.at(grid.branches[i].from);
        int t = grid.id_to_idx.at(grid.branches[i].to);
        dsu.unite(f, t);
    }

    int ref_root = dsu.find(grid.ref_bus_idx);
    double isolated_load = 0.0;
    int isolated_buses = 0;
    bool causes_islanding = false;

    for (int i = 0; i < n; ++i) {
        if (dsu.find(i) != ref_root) {
            causes_islanding = true;
            isolated_buses++;
            isolated_load += grid.buses[i].Pd;
        }
    }

    if (causes_islanding) {
        auto t_end = Clock::now();
        res.status = ContingencyStatus::Islanding;
        res.isolated_load_mw = isolated_load;
        res.isolated_bus_count = isolated_buses;
        res.solve_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
        // Severity untuk islanding: bobot sangat tinggi karena terjadi pemadaman beban
        res.PI_Total = 1000.0 + 10.0 * isolated_load;
        return res;
    }

    // 2. BANGUN Ybus LENGKAP
    std::vector<Eigen::Triplet<std::complex<double>>> y_triplets;
    y_triplets.reserve(grid.branches.size() * 4 + static_cast<size_t>(n));

    for (size_t i = 0; i < grid.branches.size(); ++i) {
        if (static_cast<int>(i) == tripped_branch_idx || grid.branches[i].status == 0) continue;
        const auto& br = grid.branches[i];
        int f = grid.id_to_idx.at(br.from);
        int t = grid.id_to_idx.at(br.to);

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

    // 3. BANGUN MATRIKS B' DAN B''
    std::vector<Eigen::Triplet<double>> bp_triplets;
    bp_triplets.reserve(grid.branches.size() * 4);

    for (size_t i = 0; i < grid.branches.size(); ++i) {
        if (static_cast<int>(i) == tripped_branch_idx || grid.branches[i].status == 0) continue;
        const auto& br = grid.branches[i];
        int f = grid.id_to_idx.at(br.from);
        int t = grid.id_to_idx.at(br.to);

        double b_series = -1.0 / br.x;
        double off_diag = b_series; 
        double diag_contrib = -b_series;

        if (f != grid.ref_bus_idx && t != grid.ref_bus_idx) {
            bp_triplets.emplace_back(ws.non_ref_map.at(f), ws.non_ref_map.at(t), off_diag);
            bp_triplets.emplace_back(ws.non_ref_map.at(t), ws.non_ref_map.at(f), off_diag);
        }
        if (f != grid.ref_bus_idx) {
            bp_triplets.emplace_back(ws.non_ref_map.at(f), ws.non_ref_map.at(f), diag_contrib);
        }
        if (t != grid.ref_bus_idx) {
            bp_triplets.emplace_back(ws.non_ref_map.at(t), ws.non_ref_map.at(t), diag_contrib);
        }
    }

    Eigen::SparseMatrix<double> Bp(n_nr, n_nr);
    Bp.setFromTriplets(bp_triplets.begin(), bp_triplets.end());
    Bp.makeCompressed();

    std::vector<Eigen::Triplet<double>> bpp_triplets;
    bpp_triplets.reserve(grid.branches.size() * 4 + static_cast<size_t>(n_pq));

    for (size_t i = 0; i < grid.branches.size(); ++i) {
        if (static_cast<int>(i) == tripped_branch_idx || grid.branches[i].status == 0) continue;
        const auto& br = grid.branches[i];
        int f = grid.id_to_idx.at(br.from);
        int t = grid.id_to_idx.at(br.to);

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
            bpp_triplets.emplace_back(ws.pq_map.at(f), ws.pq_map.at(t), bpp_ft);
            bpp_triplets.emplace_back(ws.pq_map.at(t), ws.pq_map.at(f), bpp_ft);
        }
        if (f_is_pq) {
            bpp_triplets.emplace_back(ws.pq_map.at(f), ws.pq_map.at(f), bpp_ff);
        }
        if (t_is_pq) {
            bpp_triplets.emplace_back(ws.pq_map.at(t), ws.pq_map.at(t), bpp_tt);
        }
    }

    for (int idx : ws.pq_buses) {
        double b_shunt = -grid.buses[idx].Bs / grid.baseMVA;
        bpp_triplets.emplace_back(ws.pq_map.at(idx), ws.pq_map.at(idx), b_shunt);
    }

    Eigen::SparseMatrix<double> Bpp(n_pq, n_pq);
    Bpp.setFromTriplets(bpp_triplets.begin(), bpp_triplets.end());
    Bpp.makeCompressed();

    // 4. CHOLESKY FACTORIZATION
    ws.solver_Bp.compute(Bp);
    if (ws.solver_Bp.info() != Eigen::Success) {
        res.status = ContingencyStatus::Diverged;
        res.PI_Total = 500.0;
        return res;
    }

    ws.solver_Bpp.compute(Bpp);
    if (ws.solver_Bpp.info() != Eigen::Success) {
        res.status = ContingencyStatus::Diverged;
        res.PI_Total = 500.0;
        return res;
    }

    // 5. INISIALISASI STATE (WARM-START DARI BASE CASE)
    for (int i = 0; i < n; ++i) {
        ws.V[i] = warm_V[i];
        ws.Theta[i] = warm_Theta[i];
    }
    ws.Theta[grid.ref_bus_idx] = 0.0;

    // 6. ITERATION LOOP FAST DECOUPLED
    Eigen::VectorXcd Vc(n);
    Eigen::VectorXd mis_P(n_nr);
    Eigen::VectorXd mis_Q(n_pq);
    Eigen::VectorXd dVa(n_nr);
    Eigen::VectorXd dVm(n_pq);

    bool converged = false;
    int p_iters = 0;
    int q_iters = 0;

    for (int iter = 1; iter <= max_iters; ++iter) {
        // P-Step
        for (int i = 0; i < n; ++i) {
            Vc(i) = std::polar(ws.V[i], ws.Theta[i]);
        }
        Eigen::VectorXcd Ic = Ybus * Vc;
        Eigen::VectorXcd Sc = Vc.cwiseProduct(Ic.conjugate());

        double max_p_mis = 0.0;
        for (int i = 0; i < n_nr; ++i) {
            int bus_idx = ws.non_ref_buses[i];
            double p_calc = Sc(bus_idx).real();
            double p_err = (p_calc - ws.P_spec[bus_idx]) / ws.V[bus_idx];
            mis_P(i) = p_err;
            max_p_mis = std::max(max_p_mis, std::abs(p_err));
        }

        double max_q_mis = 0.0;
        for (int i = 0; i < n_pq; ++i) {
            int bus_idx = ws.pq_buses[i];
            double q_calc = Sc(bus_idx).imag();
            double q_err = (q_calc - ws.Q_spec[bus_idx]) / ws.V[bus_idx];
            max_q_mis = std::max(max_q_mis, std::abs(q_err));
        }

        if (max_p_mis < tol && max_q_mis < tol) {
            converged = true;
            break;
        }

        dVa = ws.solver_Bp.solve(-mis_P);
        for (int i = 0; i < n_nr; ++i) {
            ws.Theta[ws.non_ref_buses[i]] += dVa(i);
        }
        p_iters++;

        // Q-Step
        for (int i = 0; i < n; ++i) {
            Vc(i) = std::polar(ws.V[i], ws.Theta[i]);
        }
        Ic = Ybus * Vc;
        Sc = Vc.cwiseProduct(Ic.conjugate());

        max_q_mis = 0.0;
        for (int i = 0; i < n_pq; ++i) {
            int bus_idx = ws.pq_buses[i];
            double q_calc = Sc(bus_idx).imag();
            double q_err = (q_calc - ws.Q_spec[bus_idx]) / ws.V[bus_idx];
            mis_Q(i) = q_err;
            max_q_mis = std::max(max_q_mis, std::abs(q_err));
        }

        max_p_mis = 0.0;
        for (int i = 0; i < n_nr; ++i) {
            int bus_idx = ws.non_ref_buses[i];
            double p_calc = Sc(bus_idx).real();
            double p_err = (p_calc - ws.P_spec[bus_idx]) / ws.V[bus_idx];
            max_p_mis = std::max(max_p_mis, std::abs(p_err));
        }

        if (max_p_mis < tol && max_q_mis < tol) {
            converged = true;
            break;
        }

        dVm = ws.solver_Bpp.solve(-mis_Q);
        for (int i = 0; i < n_pq; ++i) {
            ws.V[ws.pq_buses[i]] += dVm(i);
        }
        q_iters++;
    }

    res.p_iters = p_iters;
    res.q_iters = q_iters;

    if (!converged) {
        auto t_end = Clock::now();
        res.status = ContingencyStatus::Diverged;
        res.solve_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
        res.PI_Total = 500.0;
        return res;
    }

    // 7. POST-PROCESSING: INDEKS TEGANGAN & PEMBEBANAN CABANG
    res.status = ContingencyStatus::Converged;
    res.min_voltage = 999.0;
    res.max_voltage = -999.0;
    double pi_v = 0.0;

    for (int i = 0; i < n; ++i) {
        double v = ws.V[i];
        if (v < res.min_voltage) { res.min_voltage = v; res.min_voltage_bus = grid.buses[i].id; }
        if (v > res.max_voltage) { res.max_voltage = v; res.max_voltage_bus = grid.buses[i].id; }

        // Deviasi tegangan normal (Vnom = 1.0, dVmax = 0.1)
        double dev = std::abs(v - 1.0) / 0.10;
        pi_v += (dev * dev);

        // Penalti pelanggaran batas tegangan ([0.90, 1.10])
        if (v < 0.90) {
            res.voltage_violation_count++;
            double viol = (0.90 - v) / 0.10;
            pi_v += 10.0 * (viol * viol * viol * viol);
        } else if (v > 1.10) {
            res.voltage_violation_count++;
            double viol = (v - 1.10) / 0.10;
            pi_v += 10.0 * (viol * viol * viol * viol);
        }
    }

    // Validasi Fisik: Jika tegangan jatuh drastis (< 0.50 p.u.) atau melonjak (> 1.80 p.u.),
    // sistem mengalami Voltage Collapse / Ketidakstabilan Tegangan non-fisik
    if (res.min_voltage < 0.50 || res.max_voltage > 1.80) {
        auto t_end = Clock::now();
        res.status = ContingencyStatus::Diverged;
        res.solve_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
        res.PI_V = pi_v;
        res.PI_Total = 5000.0 + 1000.0 * std::max(0.0, 0.50 - res.min_voltage);
        return res;
    }

    res.PI_V = pi_v;

    // Evaluasi Aliran Cabang
    double pi_p = 0.0;
    double max_loading = 0.0;
    int overloaded_count = 0;
    double total_ploss = 0.0;

    Eigen::VectorXcd V_final(n);
    for (int i = 0; i < n; ++i) {
        V_final(i) = std::polar(ws.V[i], ws.Theta[i]);
    }

    for (size_t i = 0; i < grid.branches.size(); ++i) {
        if (static_cast<int>(i) == tripped_branch_idx || grid.branches[i].status == 0) continue;
        const auto& br = grid.branches[i];
        int f = grid.id_to_idx.at(br.from);
        int t = grid.id_to_idx.at(br.to);

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

        total_ploss += (Sf + St).real();
        double s_max = std::max(std::abs(Sf), std::abs(St));

        if (br.rateA > 0.0) {
            double load_pct = (s_max / br.rateA) * 100.0;
            max_loading = std::max(max_loading, load_pct);
            if (load_pct > 100.0) {
                overloaded_count++;
                double ratio = s_max / br.rateA;
                pi_p += (ratio * ratio * ratio * ratio);
            }
        }
    }

    res.max_loading_pct = max_loading;
    res.overloaded_branch_count = overloaded_count;
    res.PI_P = pi_p;
    res.Ploss_mw = total_ploss;
    res.PI_Total = res.PI_V + 2.0 * res.PI_P;

    auto t_end = Clock::now();
    res.solve_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    return res;
}

// ============================================================================
// BASE CASE SOLVER (MENDAPATKAN WARM-START & METRIK DASAR)
// ============================================================================
bool solve_base_case(
    const GridModel& grid,
    std::vector<double>& warm_V,
    std::vector<double>& warm_Theta,
    double& base_ploss_mw)
{
    SolverWorkspace ws;
    ws.init(grid);
    for (size_t i = 0; i < grid.buses.size(); ++i) {
        warm_V[i] = grid.buses[i].Vm;
        warm_Theta[i] = grid.buses[i].Va;
    }

    // Solve base case: tidak ada branch yang tripped (-1)
    auto res = simulate_single_fdxb(grid, -1, ws, warm_V, warm_Theta, 1e-6, 30);
    if (res.status != ContingencyStatus::Converged) {
        return false;
    }

    for (size_t i = 0; i < grid.buses.size(); ++i) {
        warm_V[i] = ws.V[i];
        warm_Theta[i] = ws.Theta[i];
    }
    base_ploss_mw = res.Ploss_mw;
    return true;
}

// ============================================================================
// CONTINGENCY ENGINE RUNNER (DENGAN PROFILING LENGKAP)
// ============================================================================
struct ContingencyRunSummary {
    int total_contingencies{0};
    int converged_count{0};
    int islanding_count{0};
    int diverged_count{0};
    double total_wall_time_ms{0.0};
    double avg_solve_time_us{0.0};
    double throughput_contingencies_per_sec{0.0};
    int num_threads{1};
    std::vector<ContingencyResult> results;
};

ContingencyRunSummary run_all_n1_contingencies(
    const GridModel& grid,
    const std::vector<double>& warm_V,
    const std::vector<double>& warm_Theta,
    int num_threads)
{
    using Clock = std::chrono::high_resolution_clock;
    int num_branches = static_cast<int>(grid.branches.size());

    ContingencyRunSummary summary;
    summary.total_contingencies = num_branches;
    summary.num_threads = num_threads;
    summary.results.resize(num_branches);

    std::atomic<int> next_contingency{0};
    std::atomic<int> completed_count{0};

    auto t_start = Clock::now();

    auto worker_task = [&](int thread_id) {
        (void)thread_id;
        // Tiap thread mengalokasikan workspace sendiri (zero lock-contention)
        SolverWorkspace ws;
        ws.init(grid);

        while (true) {
            int k = next_contingency.fetch_add(1, std::memory_order_relaxed);
            if (k >= num_branches) break;

            summary.results[k] = simulate_single_fdxb(grid, k, ws, warm_V, warm_Theta);
            completed_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    if (num_threads <= 1) {
        worker_task(0);
    } else {
        std::vector<std::jthread> pool;
        pool.reserve(static_cast<size_t>(num_threads));
        for (int t = 0; t < num_threads; ++t) {
            pool.emplace_back(worker_task, t);
        }
        // std::jthread otomatis join saat scope berakhir
    }

    auto t_end = Clock::now();
    summary.total_wall_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    double total_solve_us = 0.0;
    for (const auto& r : summary.results) {
        total_solve_us += r.solve_time_us;
        if (r.status == ContingencyStatus::Converged) summary.converged_count++;
        else if (r.status == ContingencyStatus::Islanding) summary.islanding_count++;
        else if (r.status == ContingencyStatus::Diverged) summary.diverged_count++;
    }

    summary.avg_solve_time_us = (num_branches > 0) ? (total_solve_us / num_branches) : 0.0;
    summary.throughput_contingencies_per_sec = (summary.total_wall_time_ms > 0.0) 
        ? (num_branches / (summary.total_wall_time_ms / 1000.0)) : 0.0;

    return summary;
}

// ============================================================================
// FORMAT LAPORAN & RANKING
// ============================================================================
void print_contingency_report(
    const std::string& case_name,
    const GridModel& grid,
    double base_ploss_mw,
    const ContingencyRunSummary& summary_1th,
    const ContingencyRunSummary& summary_nth,
    int top_k = 15)
{
    std::cout << "\n======================================================================================================\n";
    std::cout << "|                 LAPORAN HASIL SIMULASI SELURUH KONTINGENSI N-1 & PROFILING                 |\n";
    std::cout << "======================================================================================================\n";
    std::cout << "  Kasus Uji               : " << case_name << "\n";
    std::cout << "  Jumlah Bus              : " << grid.buses.size() << " bus\n";
    std::cout << "  Jumlah Cabang           : " << grid.branches.size() << " cabang (Skenario N-1: " << grid.branches.size() << " skenario)\n";
    std::cout << "  Total Beban Sistem      : " << std::fixed << std::setprecision(2) << grid.total_load_mw << " MW\n";
    std::cout << "  Total Rugi Base-Case    : " << base_ploss_mw << " MW\n";
    std::cout << "======================================================================================================\n";

    std::cout << "\n[1. RINGKASAN KLASIFIKASI KONTINGENSI]\n";
    printf("  - Total Skenario Diuji   : %d skenario (100%%)\n", summary_nth.total_contingencies);
    printf("  - Konvergen Aman/Stabil  : %d skenario (%.2f%%)\n", summary_nth.converged_count, summary_nth.converged_count * 100.0 / summary_nth.total_contingencies);
    printf("  - Islanding (Loss-of-Load): %d skenario (%.2f%%)\n", summary_nth.islanding_count, summary_nth.islanding_count * 100.0 / summary_nth.total_contingencies);
    printf("  - Divergen / Kolaps      : %d skenario (%.2f%%)\n", summary_nth.diverged_count, summary_nth.diverged_count * 100.0 / summary_nth.total_contingencies);

    std::cout << "\n[2. PROFILING KINERJA & SKALABILITAS MULTITHREADING]\n";
    std::cout << "------------------------------------------------------------------------------------------------------\n";
    std::cout << "Metrik Kinerja                         Single-Thread (1 Core)       Multi-Thread (" << summary_nth.num_threads << " Cores)     Speedup\n";
    std::cout << "-----------------------------------  --------------------------   --------------------------  --------\n";
    printf("  Total Waktu Wall-Clock              %14.2f ms             %14.2f ms          %6.2fx\n",
           summary_1th.total_wall_time_ms, summary_nth.total_wall_time_ms,
           (summary_nth.total_wall_time_ms > 0) ? (summary_1th.total_wall_time_ms / summary_nth.total_wall_time_ms) : 1.0);
    printf("  Rata-rata Waktu per Kontingensi     %14.1f µs             %14.1f µs          %6.2fx\n",
           summary_1th.avg_solve_time_us, summary_nth.avg_solve_time_us / summary_nth.num_threads,
           (summary_nth.avg_solve_time_us > 0) ? (summary_1th.avg_solve_time_us * summary_nth.num_threads / summary_nth.avg_solve_time_us) : 1.0);
    printf("  Throughput (Aliran Daya / Detik)    %14.1f solve/s        %14.1f solve/s     %6.2fx\n",
           summary_1th.throughput_contingencies_per_sec, summary_nth.throughput_contingencies_per_sec,
           (summary_1th.throughput_contingencies_per_sec > 0) ? (summary_nth.throughput_contingencies_per_sec / summary_1th.throughput_contingencies_per_sec) : 1.0);
    
    double eff = (summary_nth.num_threads > 1 && summary_nth.total_wall_time_ms > 0)
        ? ((summary_1th.total_wall_time_ms / summary_nth.total_wall_time_ms) / summary_nth.num_threads * 100.0) : 100.0;
    printf("  Efisiensi Paralel CPU                                                         %12.1f%%\n", eff);
    std::cout << "------------------------------------------------------------------------------------------------------\n";

    // 3. RANKING KONTINGENSI PALING KRITIS
    std::vector<ContingencyResult> sorted_results = summary_nth.results;
    std::sort(sorted_results.begin(), sorted_results.end(), [](const auto& a, const auto& b) {
        return a.PI_Total > b.PI_Total;
    });

    std::cout << "\n[3. TOP " << top_k << " KONTINGENSI N-1 PALING KRITIS (BERDASARKAN TOTAL SEVERITY INDEX)]\n";
    std::cout << "------------------------------------------------------------------------------------------------------\n";
    std::cout << "Rank  Branch ID  From->To    Status       Max Load   Overload   Vmin (Bus)    Vmax (Bus)   PI_V     PI_Total\n";
    std::cout << "----  ---------  ----------  -----------  ---------  --------  ------------  ------------  -------  --------\n";
    
    for (int r = 0; r < std::min(top_k, static_cast<int>(sorted_results.size())); ++r) {
        const auto& c = sorted_results[r];
        std::string status_str;
        if (c.status == ContingencyStatus::Converged) status_str = "CONVERGED";
        else if (c.status == ContingencyStatus::Islanding) status_str = "ISLANDING";
        else status_str = "DIVERGED";

        char load_buf[32];
        if (c.status == ContingencyStatus::Converged) {
            snprintf(load_buf, sizeof(load_buf), "%7.1f%%", c.max_loading_pct);
        } else if (c.status == ContingencyStatus::Islanding) {
            snprintf(load_buf, sizeof(load_buf), "Loss:%.0fMW", c.isolated_load_mw);
        } else {
            snprintf(load_buf, sizeof(load_buf), "COLLAPSE");
        }

        char vmin_buf[32], vmax_buf[32];
        if (c.status == ContingencyStatus::Converged) {
            snprintf(vmin_buf, sizeof(vmin_buf), "%.3f (#%d)", c.min_voltage, c.min_voltage_bus);
            snprintf(vmax_buf, sizeof(vmax_buf), "%.3f (#%d)", c.max_voltage, c.max_voltage_bus);
        } else {
            snprintf(vmin_buf, sizeof(vmin_buf), "   -    ");
            snprintf(vmax_buf, sizeof(vmax_buf), "   -    ");
        }

        printf("%3d    #%-6d   %4d->%-4d  %-11s %-10s   %4d     %-12s  %-12s %7.2f  %8.2f\n",
               r + 1, c.branch_idx + 1, c.from_bus, c.to_bus, status_str.c_str(),
               load_buf, c.overloaded_branch_count, vmin_buf, vmax_buf, c.PI_V, c.PI_Total);
    }

    // 4. TOP KONTINGENSI OVERLOAD CABANG (THERMAL VIOLATIONS)
    std::vector<ContingencyResult> thermal_results = summary_nth.results;
    std::sort(thermal_results.begin(), thermal_results.end(), [](const auto& a, const auto& b) {
        return a.max_loading_pct > b.max_loading_pct;
    });

    int count_overload_cases = 0;
    for (const auto& c : thermal_results) {
        if (c.status == ContingencyStatus::Converged && c.max_loading_pct > 100.0) count_overload_cases++;
    }

    std::cout << "\n[4. TOP KONTINGENSI N-1 DENGAN BEBAN TERBERAT / OVERLOAD (" << count_overload_cases << " KONTINGENSI MENYEBABKAN OVERLOAD)]\n";
    std::cout << "------------------------------------------------------------------------------------------------------\n";
    std::cout << "Rank  Branch ID  From->To    Status       Max Load   Overload   Ploss (MW)    PI_P     PI_Total\n";
    std::cout << "----  ---------  ----------  -----------  ---------  --------  ------------  -------  --------\n";
    for (int r = 0; r < std::min(top_k, static_cast<int>(thermal_results.size())); ++r) {
        const auto& c = thermal_results[r];
        if (c.status != ContingencyStatus::Converged) continue;
        printf("%3d    #%-6d   %4d->%-4d  %-11s   %7.1f%%   %4d     %10.2f    %7.2f  %8.2f\n",
               r + 1, c.branch_idx + 1, c.from_bus, c.to_bus, "CONVERGED",
               c.max_loading_pct, c.overloaded_branch_count, c.Ploss_mw, c.PI_P, c.PI_Total);
    }
    std::cout << "======================================================================================================\n";
}

// ============================================================================
// MAIN PROGRAM
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Penggunaan: " << argv[0] << " <matpower_case.m> [--threads <N>] [--top <K>]\n";
        return 1;
    }

    std::string filepath = argv[1];
    int requested_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (requested_threads <= 0) requested_threads = 4;
    int top_k = 15;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            requested_threads = std::stoi(argv[++i]);
        } else if (arg == "--top" && i + 1 < argc) {
            top_k = std::stoi(argv[++i]);
        }
    }

    std::cout << "================================================================================\n";
    std::cout << "|   C++20 HIGH-PERFORMANCE N-1 CONTINGENCY SIMULATION & PROFILING ENGINE       |\n";
    std::cout << "================================================================================\n";

    GridModel grid;
    if (!grid.load(filepath)) {
        return 1;
    }

    std::cout << "Kasus: " << filepath << " berhasil dimuat.\n";
    std::cout << "Memulai Base-Case Power Flow (Warm-Start Initialization)...\n";

    int n = static_cast<int>(grid.buses.size());
    std::vector<double> warm_V(n, 1.0);
    std::vector<double> warm_Theta(n, 0.0);
    double base_ploss_mw = 0.0;

    if (!solve_base_case(grid, warm_V, warm_Theta, base_ploss_mw)) {
        std::cerr << "⚠️ Base Case Power Flow Gagal Konvergen! Periksa integritas data grid.\n";
        return 1;
    }

    std::cout << "✓ Base Case Selesai: Ploss = " << std::fixed << std::setprecision(3) 
              << base_ploss_mw << " MW.\n";

    std::cout << "\n[Menjalankan Simulasi Seluruh " << grid.branches.size() << " Kontingensi N-1]...\n";
    
    // 1. Single-Thread Run untuk Baseline Profiling
    std::cout << "-> Menjalankan Single-Thread Baseline (1 Core)...\n";
    auto summary_1th = run_all_n1_contingencies(grid, warm_V, warm_Theta, 1);

    // 2. Multi-Thread Run dengan CPU Cores Penuh
    std::cout << "-> Menjalankan Multi-Thread Paralel (" << requested_threads << " Cores)...\n";
    auto summary_nth = run_all_n1_contingencies(grid, warm_V, warm_Theta, requested_threads);

    // Cetak Laporan Komprehensif
    print_contingency_report(filepath, grid, base_ploss_mw, summary_1th, summary_nth, top_k);

    return 0;
}
