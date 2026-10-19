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
#include <Eigen/SparseCholesky>
#if defined(USE_CHOLMOD) && __has_include(<Eigen/CholmodSupport>) && __has_include(<cholmod.h>)
#include <Eigen/CholmodSupport>
using BaseCholeskySolver = Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>>;
#else
using BaseCholeskySolver = Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>>;
#endif
#include <Eigen/SparseLU>

// ============================================================================
// STRUKTUR DATA POWER GRID & KONTINGENSI N-2
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

struct N2ContingencyResult {
    int branch1_idx{-1};
    int branch2_idx{-1};
    int from_bus1{0};
    int to_bus1{0};
    int from_bus2{0};
    int to_bus2{0};
    
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

struct GlobalBaseFactors {
    BaseCholeskySolver solver_Bp;
    BaseCholeskySolver solver_Bpp;
    
    Eigen::SparseLU<Eigen::SparseMatrix<double>> solver_lu_Bp;
    Eigen::SparseLU<Eigen::SparseMatrix<double>> solver_lu_Bpp;
    
    bool use_lu = false;
    bool initialized = false;
};


// ============================================================================
// SIMULASI FAST DECOUPLED UNTUK 1 KONTINGENSI N-2 (DUA SALURAN TRIP SEKALIGUS)
// ============================================================================
N2ContingencyResult simulate_single_n2_fdxb(
    const GridModel& grid,
    int tripped1,
    int tripped2,
    SolverWorkspace& ws,
    const GlobalBaseFactors& base_factors,
    const std::vector<double>& warm_V,
    const std::vector<double>& warm_Theta,
    double tol = 1e-4,
    int max_iters = 30)
{
    using Clock = std::chrono::high_resolution_clock;
    auto t_start = Clock::now();

    N2ContingencyResult res;
    res.branch1_idx = tripped1;
    res.branch2_idx = tripped2;
    if (tripped1 >= 0) {
        res.from_bus1 = grid.branches[tripped1].from;
        res.to_bus1 = grid.branches[tripped1].to;
    }
    if (tripped2 >= 0) {
        res.from_bus2 = grid.branches[tripped2].from;
        res.to_bus2 = grid.branches[tripped2].to;
    }

    int n = static_cast<int>(grid.buses.size());
    int n_nr = static_cast<int>(ws.non_ref_buses.size());
    int n_pq = static_cast<int>(ws.pq_buses.size());

    // 1. CEK KONEKTIVITAS & ISLANDING DENGAN DSU
    DSU dsu(n);
    for (size_t i = 0; i < grid.branches.size(); ++i) {
        if (static_cast<int>(i) == tripped1 || static_cast<int>(i) == tripped2 || grid.branches[i].status == 0) continue;
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
        if (static_cast<int>(i) == tripped1 || static_cast<int>(i) == tripped2 || grid.branches[i].status == 0) continue;
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

    
    // 3. WOODBURY MATRICES FOR B' AND B''
    int num_trips = (tripped1 >= 0 ? 1 : 0) + (tripped2 >= 0 ? 1 : 0);
    
    Eigen::MatrixXd U_p, C_p_inv;
    Eigen::MatrixXd U_pp, C_pp_inv;
    
    if (num_trips > 0) {
        U_p = Eigen::MatrixXd::Zero(n_nr, num_trips);
        Eigen::MatrixXd C_p = Eigen::MatrixXd::Zero(num_trips, num_trips);
        int p_col = 0;
        
        auto process_trip_p = [&](int trip_idx) {
            if (trip_idx < 0) return;
            const auto& br = grid.branches[trip_idx];
            int f = grid.id_to_idx.at(br.from);
            int t = grid.id_to_idx.at(br.to);
            double b_series = -1.0 / br.x;
            if (f != grid.ref_bus_idx) U_p(ws.non_ref_map.at(f), p_col) = 1.0;
            if (t != grid.ref_bus_idx) U_p(ws.non_ref_map.at(t), p_col) = -1.0;
            C_p(p_col, p_col) = b_series; 
            p_col++;
        };
        process_trip_p(tripped1);
        process_trip_p(tripped2);
        C_p_inv = C_p.inverse();
        
        // Bpp Woodbury setup (Dynamically sized based on active PQ terminals)
        std::vector<int> bpp_pq_nodes;
        std::vector<std::vector<double>> bpp_c_blocks; // 1x1 or 2x2 blocks

        auto process_trip_bpp = [&](int trip_idx) {
            if (trip_idx < 0) return;
            const auto& br = grid.branches[trip_idx];
            int f = grid.id_to_idx.at(br.from);
            int t = grid.id_to_idx.at(br.to);
            double r2_x2 = br.r * br.r + br.x * br.x;
            double b_val = -br.x / r2_x2;
            double b_sh = br.b / 2.0;
            double a = br.ratio;

            double bpp_ff = -(b_val + b_sh) / (a * a);
            double bpp_tt = -(b_val + b_sh);
            double bpp_ft = b_val / a;

            bool f_pq = (grid.buses[f].type == 1);
            bool t_pq = (grid.buses[t].type == 1);

            if (f_pq && t_pq) {
                bpp_pq_nodes.push_back(ws.pq_map.at(f));
                bpp_pq_nodes.push_back(ws.pq_map.at(t));
                // 2x2 block
                bpp_c_blocks.push_back({-bpp_ff, -bpp_ft, -bpp_ft, -bpp_tt});
            } else if (f_pq) {
                bpp_pq_nodes.push_back(ws.pq_map.at(f));
                // 1x1 block
                bpp_c_blocks.push_back({-bpp_ff});
            } else if (t_pq) {
                bpp_pq_nodes.push_back(ws.pq_map.at(t));
                // 1x1 block
                bpp_c_blocks.push_back({-bpp_tt});
            }
        };
        process_trip_bpp(tripped1);
        process_trip_bpp(tripped2);

        int pp_rank = bpp_pq_nodes.size();
        if (pp_rank > 0) {
            U_pp = Eigen::MatrixXd::Zero(n_pq, pp_rank);
            Eigen::MatrixXd C_pp = Eigen::MatrixXd::Zero(pp_rank, pp_rank);

            int col_idx = 0;
            int block_idx = 0;
            auto add_trip_blocks = [&](int trip_idx) {
                if (trip_idx < 0) return;
                const auto& br = grid.branches[trip_idx];
                int f = grid.id_to_idx.at(br.from);
                int t = grid.id_to_idx.at(br.to);
                bool f_pq = (grid.buses[f].type == 1);
                bool t_pq = (grid.buses[t].type == 1);

                if (f_pq && t_pq) {
                    U_pp(ws.pq_map.at(f), col_idx) = 1.0;
                    U_pp(ws.pq_map.at(t), col_idx + 1) = 1.0;
                    const auto& blk = bpp_c_blocks[block_idx++];
                    C_pp(col_idx, col_idx) = blk[0];
                    C_pp(col_idx, col_idx + 1) = blk[1];
                    C_pp(col_idx + 1, col_idx) = blk[2];
                    C_pp(col_idx + 1, col_idx + 1) = blk[3];
                    col_idx += 2;
                } else if (f_pq) {
                    U_pp(ws.pq_map.at(f), col_idx) = 1.0;
                    const auto& blk = bpp_c_blocks[block_idx++];
                    C_pp(col_idx, col_idx) = blk[0];
                    col_idx += 1;
                } else if (t_pq) {
                    U_pp(ws.pq_map.at(t), col_idx) = 1.0;
                    const auto& blk = bpp_c_blocks[block_idx++];
                    C_pp(col_idx, col_idx) = blk[0];
                    col_idx += 1;
                }
            };
            add_trip_blocks(tripped1);
            add_trip_blocks(tripped2);

            C_pp_inv = C_pp.inverse();
        }
    }
    
    // Precompute Z and Woodbury Cores
    Eigen::MatrixXd Z_p, CoreInv_p;
    Eigen::MatrixXd Z_pp, CoreInv_pp;
    
    if (num_trips > 0) {
        if (base_factors.use_lu) Z_p = base_factors.solver_lu_Bp.solve(U_p);
        else Z_p = base_factors.solver_Bp.solve(U_p);
        CoreInv_p = (C_p_inv + U_p.transpose() * Z_p).inverse();
        
        if (U_pp.cols() > 0) {
            if (base_factors.use_lu) Z_pp = base_factors.solver_lu_Bpp.solve(U_pp);
            else Z_pp = base_factors.solver_Bpp.solve(U_pp);
            CoreInv_pp = (C_pp_inv + U_pp.transpose() * Z_pp).inverse();
        }

        if (!CoreInv_p.allFinite() || (U_pp.cols() > 0 && !CoreInv_pp.allFinite())) {
            auto t_end = Clock::now();
            res.status = ContingencyStatus::Diverged;
            res.solve_time_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
            res.PI_Total = 500.0;
            return res;
        }
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

        
        Eigen::VectorXd dVa_base;
        if (base_factors.use_lu) dVa_base = base_factors.solver_lu_Bp.solve(-mis_P);
        else dVa_base = base_factors.solver_Bp.solve(-mis_P);
        if (num_trips > 0) {
            dVa = dVa_base - Z_p * (CoreInv_p * (U_p.transpose() * dVa_base));
        } else {
            dVa = dVa_base;
        }

        if (!dVa.allFinite()) {
            converged = false;
            break;
        }

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
            if (std::isnan(q_err) || std::isinf(q_err)) {
                converged = false;
                break;
            }
            mis_Q(i) = q_err;
            max_q_mis = std::max(max_q_mis, std::abs(q_err));
        }

        max_p_mis = 0.0;
        for (int i = 0; i < n_nr; ++i) {
            int bus_idx = ws.non_ref_buses[i];
            double p_calc = Sc(bus_idx).real();
            double p_err = (p_calc - ws.P_spec[bus_idx]) / ws.V[bus_idx];
            if (std::isnan(p_err) || std::isinf(p_err)) {
                converged = false;
                break;
            }
            mis_P(i) = p_err;
            max_p_mis = std::max(max_p_mis, std::abs(p_err));
        }

        if (max_p_mis < tol && max_q_mis < tol) {
            converged = true;
            break;
        }

        
        Eigen::VectorXd dVm_base;
        if (base_factors.use_lu) dVm_base = base_factors.solver_lu_Bpp.solve(-mis_Q);
        else dVm_base = base_factors.solver_Bpp.solve(-mis_Q);
        if (U_pp.cols() > 0) {
            dVm = dVm_base - Z_pp * (CoreInv_pp * (U_pp.transpose() * dVm_base));
        } else {
            dVm = dVm_base;
        }

        if (!dVm.allFinite()) {
            converged = false;
            break;
        }

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
        if (static_cast<int>(i) == tripped1 || static_cast<int>(i) == tripped2 || grid.branches[i].status == 0) continue;
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
// BASE CASE SOLVER
// ============================================================================
bool solve_base_case(
    const GridModel& grid,
    std::vector<double>& warm_V,
    std::vector<double>& warm_Theta,
    double& base_ploss_mw)
{

    GlobalBaseFactors base_factors;
    {
        SolverWorkspace ws_base;
        ws_base.init(grid);
        
        int n_nr = ws_base.non_ref_buses.size();
        int n_pq = ws_base.pq_buses.size();
        std::vector<Eigen::Triplet<double>> bp_triplets, bpp_triplets;
        
        for (size_t i = 0; i < grid.branches.size(); ++i) {
            if (grid.branches[i].status == 0) continue;
            const auto& br = grid.branches[i];
            int f = grid.id_to_idx.at(br.from);
            int t = grid.id_to_idx.at(br.to);
            double b_series = -1.0 / br.x;
            if (f != grid.ref_bus_idx && t != grid.ref_bus_idx) {
                bp_triplets.emplace_back(ws_base.non_ref_map.at(f), ws_base.non_ref_map.at(t), b_series);
                bp_triplets.emplace_back(ws_base.non_ref_map.at(t), ws_base.non_ref_map.at(f), b_series);
            }
            if (f != grid.ref_bus_idx) bp_triplets.emplace_back(ws_base.non_ref_map.at(f), ws_base.non_ref_map.at(f), -b_series);
            if (t != grid.ref_bus_idx) bp_triplets.emplace_back(ws_base.non_ref_map.at(t), ws_base.non_ref_map.at(t), -b_series);
            
            double r2_x2 = br.r * br.r + br.x * br.x;
            double b_val = -br.x / r2_x2;
            double b_sh = br.b / 2.0;
            double a = br.ratio;
            double bpp_ff = -(b_val + b_sh) / (a * a);
            double bpp_tt = -(b_val + b_sh);
            double bpp_ft = b_val / a;
            
            if (grid.buses[f].type == 1 && grid.buses[t].type == 1) {
                bpp_triplets.emplace_back(ws_base.pq_map.at(f), ws_base.pq_map.at(t), bpp_ft);
                bpp_triplets.emplace_back(ws_base.pq_map.at(t), ws_base.pq_map.at(f), bpp_ft);
            }
            if (grid.buses[f].type == 1) bpp_triplets.emplace_back(ws_base.pq_map.at(f), ws_base.pq_map.at(f), bpp_ff);
            if (grid.buses[t].type == 1) bpp_triplets.emplace_back(ws_base.pq_map.at(t), ws_base.pq_map.at(t), bpp_tt);
        }
        for (int idx : ws_base.pq_buses) {
            bpp_triplets.emplace_back(ws_base.pq_map.at(idx), ws_base.pq_map.at(idx), -grid.buses[idx].Bs / grid.baseMVA);
        }
        
        Eigen::SparseMatrix<double> Bp(n_nr, n_nr), Bpp(n_pq, n_pq);
        Bp.setFromTriplets(bp_triplets.begin(), bp_triplets.end());
        Bpp.setFromTriplets(bpp_triplets.begin(), bpp_triplets.end());
        
        base_factors.solver_Bp.compute(Bp);
        base_factors.solver_Bpp.compute(Bpp);
        if (base_factors.solver_Bp.info() != Eigen::Success || base_factors.solver_Bpp.info() != Eigen::Success) {
            std::cout << "\n⚠️ CHOLMOD gagal (Matriks tidak SPD). Melakukan Fallback Otomatis ke Eigen::SparseLU...\n";
            base_factors.solver_lu_Bp.analyzePattern(Bp);
            base_factors.solver_lu_Bp.factorize(Bp);
            base_factors.solver_lu_Bpp.analyzePattern(Bpp);
            base_factors.solver_lu_Bpp.factorize(Bpp);
            if (base_factors.solver_lu_Bp.info() != Eigen::Success || base_factors.solver_lu_Bpp.info() != Eigen::Success) {
                std::cout << "❌ SparseLU juga gagal memfaktorkan matriks. Grid tidak valid.\n";
            } else {
                base_factors.use_lu = true;
                std::cout << "✓ Fallback ke SparseLU berhasil. Melanjutkan simulasi...\n";
            }
        }
        base_factors.initialized = true;
    }

    SolverWorkspace ws;
    ws.init(grid);
    for (size_t i = 0; i < grid.buses.size(); ++i) {
        warm_V[i] = grid.buses[i].Vm;
        warm_Theta[i] = grid.buses[i].Va;
    }

    auto res = simulate_single_n2_fdxb(grid, -1, -1, ws, base_factors, warm_V, warm_Theta, 1e-6, 30);
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
// RUNNER SIMULASI N-2 DENGAN ATOMIC WORK-STEALING PARALEL
// ============================================================================
struct N2RunSummary {
    size_t total_contingencies{0};
    int converged_count{0};
    int islanding_count{0};
    int diverged_count{0};
    double total_wall_time_ms{0.0};
    double avg_solve_time_us{0.0};
    double throughput_contingencies_per_sec{0.0};
    int num_threads{1};
    std::vector<N2ContingencyResult> results;
};

N2RunSummary run_n2_contingencies_parallel(
    const GridModel& grid,
    const std::vector<std::pair<int, int>>& pairs_to_simulate,
    const std::vector<double>& warm_V,
    const std::vector<double>& warm_Theta,
    int num_threads)
{
    GlobalBaseFactors base_factors;
    {
        SolverWorkspace ws_base;
        ws_base.init(grid);
        
        int n_nr = ws_base.non_ref_buses.size();
        int n_pq = ws_base.pq_buses.size();
        std::vector<Eigen::Triplet<double>> bp_triplets, bpp_triplets;
        
        for (size_t i = 0; i < grid.branches.size(); ++i) {
            if (grid.branches[i].status == 0) continue;
            const auto& br = grid.branches[i];
            int f = grid.id_to_idx.at(br.from);
            int t = grid.id_to_idx.at(br.to);
            double b_series = -1.0 / br.x;
            if (f != grid.ref_bus_idx && t != grid.ref_bus_idx) {
                bp_triplets.emplace_back(ws_base.non_ref_map.at(f), ws_base.non_ref_map.at(t), b_series);
                bp_triplets.emplace_back(ws_base.non_ref_map.at(t), ws_base.non_ref_map.at(f), b_series);
            }
            if (f != grid.ref_bus_idx) bp_triplets.emplace_back(ws_base.non_ref_map.at(f), ws_base.non_ref_map.at(f), -b_series);
            if (t != grid.ref_bus_idx) bp_triplets.emplace_back(ws_base.non_ref_map.at(t), ws_base.non_ref_map.at(t), -b_series);
            
            double r2_x2 = br.r * br.r + br.x * br.x;
            double b_val = -br.x / r2_x2;
            double b_sh = br.b / 2.0;
            double a = br.ratio;
            double bpp_ff = -(b_val + b_sh) / (a * a);
            double bpp_tt = -(b_val + b_sh);
            double bpp_ft = b_val / a;
            
            if (grid.buses[f].type == 1 && grid.buses[t].type == 1) {
                bpp_triplets.emplace_back(ws_base.pq_map.at(f), ws_base.pq_map.at(t), bpp_ft);
                bpp_triplets.emplace_back(ws_base.pq_map.at(t), ws_base.pq_map.at(f), bpp_ft);
            }
            if (grid.buses[f].type == 1) bpp_triplets.emplace_back(ws_base.pq_map.at(f), ws_base.pq_map.at(f), bpp_ff);
            if (grid.buses[t].type == 1) bpp_triplets.emplace_back(ws_base.pq_map.at(t), ws_base.pq_map.at(t), bpp_tt);
        }
        for (int idx : ws_base.pq_buses) {
            bpp_triplets.emplace_back(ws_base.pq_map.at(idx), ws_base.pq_map.at(idx), -grid.buses[idx].Bs / grid.baseMVA);
        }
        
        Eigen::SparseMatrix<double> Bp(n_nr, n_nr), Bpp(n_pq, n_pq);
        Bp.setFromTriplets(bp_triplets.begin(), bp_triplets.end());
        Bpp.setFromTriplets(bpp_triplets.begin(), bpp_triplets.end());
        
        base_factors.solver_Bp.compute(Bp);
        base_factors.solver_Bpp.compute(Bpp);
        if (base_factors.solver_Bp.info() != Eigen::Success || base_factors.solver_Bpp.info() != Eigen::Success) {
            std::cout << "\n⚠️ CHOLMOD gagal (Matriks tidak SPD). Melakukan Fallback Otomatis ke Eigen::SparseLU...\n";
            base_factors.solver_lu_Bp.analyzePattern(Bp);
            base_factors.solver_lu_Bp.factorize(Bp);
            base_factors.solver_lu_Bpp.analyzePattern(Bpp);
            base_factors.solver_lu_Bpp.factorize(Bpp);
            if (base_factors.solver_lu_Bp.info() != Eigen::Success || base_factors.solver_lu_Bpp.info() != Eigen::Success) {
                std::cout << "❌ SparseLU juga gagal memfaktorkan matriks. Grid tidak valid.\n";
            } else {
                base_factors.use_lu = true;
                std::cout << "✓ Fallback ke SparseLU berhasil. Melanjutkan simulasi...\n";
            }
        }
        base_factors.initialized = true;
    }

    using Clock = std::chrono::high_resolution_clock;
    size_t total_tasks = pairs_to_simulate.size();

    N2RunSummary summary;
    summary.total_contingencies = total_tasks;
    summary.num_threads = num_threads;
    summary.results.resize(total_tasks);

    std::atomic<size_t> next_contingency{0};
    std::atomic<size_t> completed_count{0};

    auto t_start = Clock::now();

    auto worker_task = [&](int thread_id) {
        (void)thread_id;
        SolverWorkspace ws;
        ws.init(grid);

        while (true) {
            size_t k = next_contingency.fetch_add(1, std::memory_order_relaxed);
            if (k >= total_tasks) break;

            int b1 = pairs_to_simulate[k].first;
            int b2 = pairs_to_simulate[k].second;

            summary.results[k] = simulate_single_n2_fdxb(grid, b1, b2, ws, base_factors, warm_V, warm_Theta);
            completed_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::atomic<bool> all_done{false};
    std::jthread progress_monitor([&](std::stop_token st) {
        auto t0 = Clock::now();
        while (!st.stop_requested() && !all_done.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (all_done.load()) break;
            size_t comp = completed_count.load(std::memory_order_relaxed);
            auto now = Clock::now();
            double elapsed_s = std::chrono::duration<double>(now - t0).count();
            double rate = (elapsed_s > 0) ? (static_cast<double>(comp) / elapsed_s) : 0;
            double pct = (total_tasks > 0) ? (static_cast<double>(comp) * 100.0 / static_cast<double>(total_tasks)) : 0;
            double rem_s = (rate > 0 && comp < total_tasks) ? (static_cast<double>(total_tasks - comp) / rate) : 0;
            std::cout << "[PROGRESS] " << comp << " / " << total_tasks
                      << " (" << std::fixed << std::setprecision(1) << pct << "%)"
                      << " | Kecepatan: " << std::setprecision(0) << rate << " solve/s"
                      << " | Estimasi Sisa: " << std::setprecision(1) << rem_s << " s\n" << std::flush;
        }
    });

    if (num_threads <= 1) {
        worker_task(0);
    } else {
        std::vector<std::jthread> pool;
        pool.reserve(static_cast<size_t>(num_threads));
        for (int t = 0; t < num_threads; ++t) {
            pool.emplace_back(worker_task, t);
        }
    }

    all_done.store(true);
    auto t_end = Clock::now();
    summary.total_wall_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    double total_solve_us = 0.0;
    for (const auto& r : summary.results) {
        total_solve_us += r.solve_time_us;
        if (r.status == ContingencyStatus::Converged) summary.converged_count++;
        else if (r.status == ContingencyStatus::Islanding) summary.islanding_count++;
        else if (r.status == ContingencyStatus::Diverged) summary.diverged_count++;
    }

    summary.avg_solve_time_us = (total_tasks > 0) ? (total_solve_us / static_cast<double>(total_tasks)) : 0.0;
    summary.throughput_contingencies_per_sec = (summary.total_wall_time_ms > 0.0) 
        ? (static_cast<double>(total_tasks) / (summary.total_wall_time_ms / 1000.0)) : 0.0;

    return summary;
}

// ============================================================================
// FORMAT LAPORAN N-2 CONTINGENCY
// ============================================================================
void print_n2_report(
    const std::string& case_name,
    const GridModel& grid,
    double base_ploss_mw,
    size_t total_possible_pairs,
    const N2RunSummary& summary,
    int top_k = 15)
{
    std::cout << "\n===============================================================================================================\n";
    std::cout << "|                       LAPORAN SIMULASI KONTINGENSI GANDA (N-2) & PROFILING C++20                            |\n";
    std::cout << "===============================================================================================================\n";
    std::cout << "  Kasus Uji                     : " << case_name << "\n";
    std::cout << "  Jumlah Bus                    : " << grid.buses.size() << " bus\n";
    std::cout << "  Jumlah Cabang                 : " << grid.branches.size() << " cabang\n";
    std::cout << "  Total Ruang Kombinatorial N-2 : " << total_possible_pairs << " pasangan skenario [C(" << grid.branches.size() << ", 2)]\n";
    std::cout << "  Skenario N-2 yang Disimulasikan: " << summary.total_contingencies << " skenario\n";
    std::cout << "  Total Beban Sistem            : " << std::fixed << std::setprecision(2) << grid.total_load_mw << " MW\n";
    std::cout << "  Total Rugi Base-Case          : " << base_ploss_mw << " MW\n";
    std::cout << "===============================================================================================================\n";

    std::cout << "\n[1. RINGKASAN KLASIFIKASI KONTINGENSI N-2]\n";
    printf("  - Skenario Disimulasikan : %zu skenario (100%%)\n", summary.total_contingencies);
    printf("  - Konvergen Aman/Stabil  : %d skenario (%.2f%%)\n", summary.converged_count, summary.converged_count * 100.0 / static_cast<double>(summary.total_contingencies));
    printf("  - Islanding (Loss-of-Load): %d skenario (%.2f%%)\n", summary.islanding_count, summary.islanding_count * 100.0 / static_cast<double>(summary.total_contingencies));
    printf("  - Divergen / Kolaps      : %d skenario (%.2f%%)\n", summary.diverged_count, summary.diverged_count * 100.0 / static_cast<double>(summary.total_contingencies));

    std::cout << "\n[2. PROFILING KINERJA & SKALABILITAS MULTITHREADING (" << summary.num_threads << " CORES CPU)]\n";
    std::cout << "---------------------------------------------------------------------------------------------------------------\n";
    printf("  Total Waktu Wall-Clock            : %12.2f ms  (%.3f detik)\n", summary.total_wall_time_ms, summary.total_wall_time_ms / 1000.0);
    printf("  Rata-rata Waktu per Kasus N-2     : %12.1f µs\n", summary.avg_solve_time_us);
    printf("  Throughput Komputasi Paralel      : %12.1f ALIRAN DAYA PER DETIK!\n", summary.throughput_contingencies_per_sec);
    std::cout << "---------------------------------------------------------------------------------------------------------------\n";

    // 3. RANKING KONTINGENSI PALING KRITIS DENGAN INDEX SORT (Zero-Copy)
    size_t total_n = summary.results.size();
    std::vector<size_t> p_idx(total_n);
    std::iota(p_idx.begin(), p_idx.end(), 0);
    size_t top_n = std::min(static_cast<size_t>(top_k), total_n);
    std::partial_sort(p_idx.begin(), p_idx.begin() + top_n, p_idx.end(), [&](size_t a, size_t b) {
        return summary.results[a].PI_Total > summary.results[b].PI_Total;
    });

    std::cout << "\n[3. TOP " << top_n << " KONTINGENSI N-2 PALING KRITIS (BERDASARKAN TOTAL SEVERITY INDEX)]\n";
    std::cout << "---------------------------------------------------------------------------------------------------------------\n";
    std::cout << "Rank  Tripped Pair       From->To (1)  From->To (2)  Status       Max Load   Overload   Vmin (Bus)    PI_V     PI_Total\n";
    std::cout << "----  -----------------  ------------  ------------  -----------  ---------  --------  ------------  -------  --------\n";
    
    for (size_t r = 0; r < top_n; ++r) {
        const auto& c = summary.results[p_idx[r]];
        std::string status_str;
        if (c.status == ContingencyStatus::Converged) status_str = "CONVERGED";
        else if (c.status == ContingencyStatus::Islanding) status_str = "ISLANDING";
        else status_str = "DIVERGED";

        char pair_buf[32];
        snprintf(pair_buf, sizeof(pair_buf), "#%d & #%d", c.branch1_idx + 1, c.branch2_idx + 1);

        char f1_buf[32], f2_buf[32];
        snprintf(f1_buf, sizeof(f1_buf), "%4d->%-4d", c.from_bus1, c.to_bus1);
        snprintf(f2_buf, sizeof(f2_buf), "%4d->%-4d", c.from_bus2, c.to_bus2);

        char load_buf[32];
        if (c.status == ContingencyStatus::Converged) {
            snprintf(load_buf, sizeof(load_buf), "%7.1f%%", c.max_loading_pct);
        } else if (c.status == ContingencyStatus::Islanding) {
            snprintf(load_buf, sizeof(load_buf), "Loss:%.0fMW", c.isolated_load_mw);
        } else {
            snprintf(load_buf, sizeof(load_buf), "COLLAPSE");
        }

        char vmin_buf[32];
        if (c.status == ContingencyStatus::Converged) {
            snprintf(vmin_buf, sizeof(vmin_buf), "%.3f (#%d)", c.min_voltage, c.min_voltage_bus);
        } else {
            snprintf(vmin_buf, sizeof(vmin_buf), "   -    ");
        }

        printf("%3zu   %-17s  %-12s  %-12s  %-11s %-10s   %4d     %-12s %7.2f  %8.2f\n",
               r + 1, pair_buf, f1_buf, f2_buf, status_str.c_str(),
               load_buf, c.overloaded_branch_count, vmin_buf, c.PI_V, c.PI_Total);
    }

    // 4. TOP KONTINGENSI OVERLOAD CABANG (THERMAL VIOLATIONS)
    std::vector<size_t> t_idx;
    t_idx.reserve(total_n);
    int count_overload_cases = 0;
    for (size_t i = 0; i < total_n; ++i) {
        if (summary.results[i].status == ContingencyStatus::Converged) {
            t_idx.push_back(i);
            if (summary.results[i].max_loading_pct > 100.0) count_overload_cases++;
        }
    }

    size_t thermal_top_n = std::min(static_cast<size_t>(top_k), t_idx.size());
    std::partial_sort(t_idx.begin(), t_idx.begin() + thermal_top_n, t_idx.end(), [&](size_t a, size_t b) {
        return summary.results[a].max_loading_pct > summary.results[b].max_loading_pct;
    });

    std::cout << "\n[4. TOP KONTINGENSI N-2 DENGAN BEBAN TERBERAT / OVERLOAD (" << count_overload_cases << " KONTINGENSI MENYEBABKAN OVERLOAD)]\n";
    std::cout << "---------------------------------------------------------------------------------------------------------------\n";
    std::cout << "Rank  Tripped Pair       From->To (1)  From->To (2)  Status       Max Load   Overload   Ploss (MW)    PI_P     PI_Total\n";
    std::cout << "----  -----------------  ------------  ------------  -----------  ---------  --------  ------------  -------  --------\n";
    for (size_t r = 0; r < thermal_top_n; ++r) {
        const auto& c = summary.results[t_idx[r]];

        char pair_buf[32];
        snprintf(pair_buf, sizeof(pair_buf), "#%d & #%d", c.branch1_idx + 1, c.branch2_idx + 1);

        char f1_buf[32], f2_buf[32];
        snprintf(f1_buf, sizeof(f1_buf), "%4d->%-4d", c.from_bus1, c.to_bus1);
        snprintf(f2_buf, sizeof(f2_buf), "%4d->%-4d", c.from_bus2, c.to_bus2);

        printf("%3zu   %-17s  %-12s  %-12s  %-11s   %7.1f%%   %4d     %10.2f    %7.2f  %8.2f\n",
               r + 1, pair_buf, f1_buf, f2_buf, "CONVERGED",
               c.max_loading_pct, c.overloaded_branch_count, c.Ploss_mw, c.PI_P, c.PI_Total);
    }
    std::cout << "===============================================================================================================\n";
}

// ============================================================================
// EXPORT HASIL SIMULASI KE FORMAT JSON (UNTUK INTEGRASI GUI / EMS)
// ============================================================================
void export_n2_json(
    const std::string& json_path,
    const std::string& case_name,
    const GridModel& grid,
    double base_ploss_mw,
    size_t total_possible_pairs,
    const N2RunSummary& summary,
    int top_k = 50)
{
    std::ofstream out(json_path);
    if (!out.is_open()) {
        std::cerr << "⚠️ Gagal membuka berkas output JSON: " << json_path << "\n";
        return;
    }

    size_t total_n = summary.results.size();
    std::vector<size_t> p_idx(total_n);
    std::iota(p_idx.begin(), p_idx.end(), 0);
    size_t top_n = std::min(static_cast<size_t>(top_k), total_n);
    std::partial_sort(p_idx.begin(), p_idx.begin() + top_n, p_idx.end(), [&](size_t a, size_t b) {
        return summary.results[a].PI_Total > summary.results[b].PI_Total;
    });

    out << "{\n";
    out << "  \"case_name\": \"" << case_name << "\",\n";
    out << "  \"num_buses\": " << grid.buses.size() << ",\n";
    out << "  \"num_branches\": " << grid.branches.size() << ",\n";
    out << "  \"total_load_mw\": " << std::fixed << std::setprecision(2) << grid.total_load_mw << ",\n";
    out << "  \"base_ploss_mw\": " << std::fixed << std::setprecision(3) << base_ploss_mw << ",\n";
    out << "  \"total_possible_pairs\": " << total_possible_pairs << ",\n";
    out << "  \"simulated_pairs\": " << summary.total_contingencies << ",\n";
    out << "  \"performance\": {\n";
    out << "    \"num_threads\": " << summary.num_threads << ",\n";
    out << "    \"total_wall_time_ms\": " << std::fixed << std::setprecision(2) << summary.total_wall_time_ms << ",\n";
    out << "    \"avg_solve_time_us\": " << std::fixed << std::setprecision(2) << summary.avg_solve_time_us << ",\n";
    out << "    \"throughput_solves_per_sec\": " << std::fixed << std::setprecision(1) << summary.throughput_contingencies_per_sec << "\n";
    out << "  },\n";
    out << "  \"classification\": {\n";
    out << "    \"converged\": " << summary.converged_count << ",\n";
    out << "    \"islanding\": " << summary.islanding_count << ",\n";
    out << "    \"diverged\": " << summary.diverged_count << "\n";
    out << "  },\n";
    out << "  \"top_critical_contingencies\": [\n";

    for (size_t r = 0; r < top_n; ++r) {
        const auto& c = summary.results[p_idx[r]];
        std::string status_str = (c.status == ContingencyStatus::Converged) ? "CONVERGED" :
                                 (c.status == ContingencyStatus::Islanding) ? "ISLANDING" : "DIVERGED";

        out << "    {\n";
        out << "      \"rank\": " << (r + 1) << ",\n";
        out << "      \"branch1_idx\": " << c.branch1_idx << ",\n";
        out << "      \"branch2_idx\": " << c.branch2_idx << ",\n";
        out << "      \"from_bus1\": " << c.from_bus1 << ",\n";
        out << "      \"to_bus1\": " << c.to_bus1 << ",\n";
        out << "      \"from_bus2\": " << c.from_bus2 << ",\n";
        out << "      \"to_bus2\": " << c.to_bus2 << ",\n";
        out << "      \"status\": \"" << status_str << "\",\n";
        out << "      \"max_loading_pct\": " << std::fixed << std::setprecision(2) << c.max_loading_pct << ",\n";
        out << "      \"overloaded_branch_count\": " << c.overloaded_branch_count << ",\n";
        out << "      \"min_voltage\": " << std::fixed << std::setprecision(4) << c.min_voltage << ",\n";
        out << "      \"min_voltage_bus\": " << c.min_voltage_bus << ",\n";
        out << "      \"isolated_load_mw\": " << std::fixed << std::setprecision(2) << c.isolated_load_mw << ",\n";
        out << "      \"PI_V\": " << std::fixed << std::setprecision(2) << c.PI_V << ",\n";
        out << "      \"PI_P\": " << std::fixed << std::setprecision(2) << c.PI_P << ",\n";
        out << "      \"PI_Total\": " << std::fixed << std::setprecision(2) << c.PI_Total << ",\n";
        out << "      \"Ploss_mw\": " << std::fixed << std::setprecision(2) << c.Ploss_mw << "\n";
        out << "    }" << (r + 1 < top_n ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    std::cout << "✓ Hasil simulasi berhasil diekspor ke JSON: " << json_path << "\n";
}

// ============================================================================
// MAIN PROGRAM
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Penggunaan: " << argv[0] << " <matpower_case.m> [--threads <N>] [--limit <N>] [--all] [--top <K>] [--json-out <file.json>]\n";
        return 1;
    }

    std::string filepath = argv[1];
    int requested_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (requested_threads <= 0) requested_threads = 4;
    int top_k = 15;
    size_t user_limit = 0;
    bool force_all = false;
    std::string json_out_path = "";

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            requested_threads = std::stoi(argv[++i]);
        } else if (arg == "--limit" && i + 1 < argc) {
            user_limit = static_cast<size_t>(std::stoll(argv[++i]));
        } else if (arg == "--all") {
            force_all = true;
        } else if (arg == "--top" && i + 1 < argc) {
            top_k = std::stoi(argv[++i]);
        } else if (arg == "--json-out" && i + 1 < argc) {
            json_out_path = argv[++i];
        }
    }

    std::cout << "================================================================================\n";
    std::cout << "|   C++20 HIGH-PERFORMANCE N-2 CONTINGENCY SIMULATION & PROFILING ENGINE       |\n";
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

    size_t m = grid.branches.size();
    size_t total_possible_pairs = m * (m - 1) / 2;

    std::cout << "Total kombinasi pasangan N-2: " << total_possible_pairs << " skenario.\n";

    // Membangun daftar pasangan N-2
    std::vector<std::pair<int, int>> pairs_to_simulate;
    size_t target_sim_count = total_possible_pairs;

    if (!force_all && user_limit > 0) {
        target_sim_count = std::min(total_possible_pairs, user_limit);
    } else if (!force_all && total_possible_pairs > 100000) {
        target_sim_count = 10000; // Default safety limit untuk sistem masif seperti PEGASE
        std::cout << "💡 Catatan: Sistem besar (" << total_possible_pairs << " pasangan). Menjalankan default " 
                  << target_sim_count << " skenario pertama (Gunakan --all untuk mensimulasikan seluruh " 
                  << total_possible_pairs << " atau --limit <N>).\n";
    }

    pairs_to_simulate.reserve(target_sim_count);
    size_t added = 0;
    for (size_t i = 0; i < m && added < target_sim_count; ++i) {
        for (size_t j = i + 1; j < m && added < target_sim_count; ++j) {
            pairs_to_simulate.emplace_back(static_cast<int>(i), static_cast<int>(j));
            added++;
        }
    }

    std::cout << "\n[Menjalankan Simulasi " << pairs_to_simulate.size() << " Kontingensi N-2 dengan " 
              << requested_threads << " Cores CPU]...\n";
    
    auto summary = run_n2_contingencies_parallel(grid, pairs_to_simulate, warm_V, warm_Theta, requested_threads);

    print_n2_report(filepath, grid, base_ploss_mw, total_possible_pairs, summary, top_k);

    if (!json_out_path.empty()) {
        export_n2_json(json_out_path, filepath, grid, base_ploss_mw, total_possible_pairs, summary, std::max(top_k, 50));
    }

    return 0;
}
