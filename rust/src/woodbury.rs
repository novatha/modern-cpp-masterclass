use faer::prelude::Solve;
use faer::sparse::linalg::solvers::{Llt, Lu};
use faer::sparse::{SparseColMat, Triplet};
use faer::{Mat, Side};

use crate::grid::{ContingencyResult, ContingencyStatus, GridModel};
use crate::islanding::check_islanding;
use std::time::Instant;

/// Robust sparse linear solver supporting Cholesky (LL^T) with LU fallback
#[derive(Debug, Clone)]
pub enum SparseLinearSolver {
    Cholesky(Llt<usize, f64>),
    Lu(Lu<usize, f64>),
}

impl SparseLinearSolver {
    #[inline]
    pub fn solve(&self, rhs: &Mat<f64>) -> Mat<f64> {
        match self {
            Self::Cholesky(chol) => chol.solve(rhs),
            Self::Lu(lu) => lu.solve(rhs),
        }
    }
}

/// Pre-factored base susceptance matrices (B' and B'')
pub struct BaseFactors {
    pub bp_solver: SparseLinearSolver,
    pub bpp_solver: Option<SparseLinearSolver>,
    pub non_ref_buses: Vec<usize>,
    pub non_ref_map: Vec<Option<usize>>, // bus index -> non_ref index
    pub pq_buses: Vec<usize>,
    pub pq_map: Vec<Option<usize>>,      // bus index -> pq index
    pub p_spec: Vec<f64>,
    pub q_spec: Vec<f64>,
}

impl BaseFactors {
    pub fn build(grid: &GridModel) -> Result<Self, &'static str> {
        let n = grid.buses.len();
        let ref_idx = grid.ref_bus_idx;

        let mut non_ref_buses = Vec::new();
        let mut non_ref_map = vec![None; n];
        let mut nr_count = 0;

        let mut pq_buses = Vec::new();
        let mut pq_map = vec![None; n];
        let mut pq_count = 0;

        let mut p_spec = vec![0.0f64; n];
        let mut q_spec = vec![0.0f64; n];

        for i in 0..n {
            p_spec[i] = (grid.buses[i].pg - grid.buses[i].pd) / grid.base_mva;
            q_spec[i] = (grid.buses[i].qg - grid.buses[i].qd) / grid.base_mva;

            if i != ref_idx {
                non_ref_buses.push(i);
                non_ref_map[i] = Some(nr_count);
                nr_count += 1;
            }
            if grid.buses[i].bus_type == 1 {
                pq_buses.push(i);
                pq_map[i] = Some(pq_count);
                pq_count += 1;
            }
        }

        // Assemble sparse B'
        let mut bp_triplets = Vec::with_capacity(grid.branches.len() * 4 + nr_count);
        for br in &grid.branches {
            if br.status == 0 {
                continue;
            }
            let f = *grid.id_to_idx.get(&br.from).unwrap();
            let t = *grid.id_to_idx.get(&br.to).unwrap();

            // B' susceptance for P-Theta
            let b_series = -1.0 / br.x;
            if let Some(fi) = non_ref_map[f] {
                bp_triplets.push(Triplet::new(fi, fi, -b_series));
            }
            if let Some(ti) = non_ref_map[t] {
                bp_triplets.push(Triplet::new(ti, ti, -b_series));
            }
            if let (Some(fi), Some(ti)) = (non_ref_map[f], non_ref_map[t]) {
                bp_triplets.push(Triplet::new(fi, ti, b_series));
                bp_triplets.push(Triplet::new(ti, fi, b_series));
            }
        }

        // Add small regularization to diagonal to ensure SPD numerical stability
        for i in 0..nr_count {
            bp_triplets.push(Triplet::new(i, i, 1e-6));
        }

        let bp_mat = SparseColMat::<usize, f64>::try_new_from_triplets(nr_count, nr_count, &bp_triplets)
            .map_err(|_| "Failed to assemble sparse B'")?;

        let bp_solver = if let Ok(chol) = bp_mat.sp_cholesky(Side::Lower) {
            SparseLinearSolver::Cholesky(chol)
        } else if let Ok(lu) = bp_mat.sp_lu() {
            SparseLinearSolver::Lu(lu)
        } else {
            return Err("Failed to factorize sparse B' (both Cholesky and LU failed)");
        };

        // Assemble sparse B''
        let bpp_solver = if pq_count > 0 {
            let mut bpp_triplets = Vec::with_capacity(grid.branches.len() * 4 + pq_count);
            for br in &grid.branches {
                if br.status == 0 {
                    continue;
                }
                let f = *grid.id_to_idx.get(&br.from).unwrap();
                let t = *grid.id_to_idx.get(&br.to).unwrap();

                let r2_x2 = br.r * br.r + br.x * br.x;
                let b_val = -br.x / r2_x2;
                let b_sh = br.b / 2.0;
                let a = br.ratio;
                let bpp_ff = -(b_val + b_sh) / (a * a);
                let bpp_tt = -(b_val + b_sh);
                let bpp_ft = b_val / a;

                if let Some(f_pq) = pq_map[f] {
                    bpp_triplets.push(Triplet::new(f_pq, f_pq, bpp_ff));
                }
                if let Some(t_pq) = pq_map[t] {
                    bpp_triplets.push(Triplet::new(t_pq, t_pq, bpp_tt));
                }
                if let (Some(f_pq), Some(t_pq)) = (pq_map[f], pq_map[t]) {
                    bpp_triplets.push(Triplet::new(f_pq, t_pq, bpp_ft));
                    bpp_triplets.push(Triplet::new(t_pq, f_pq, bpp_ft));
                }
            }

            // Add bus shunts to B''
            for &idx in &pq_buses {
                let pq_i = pq_map[idx].unwrap();
                bpp_triplets.push(Triplet::new(pq_i, pq_i, -grid.buses[idx].bs / grid.base_mva));
            }

            for i in 0..pq_count {
                bpp_triplets.push(Triplet::new(i, i, 1e-6));
            }

            let bpp_mat = SparseColMat::<usize, f64>::try_new_from_triplets(pq_count, pq_count, &bpp_triplets)
                .map_err(|_| "Failed to assemble sparse B''")?;

            let solver = if let Ok(chol) = bpp_mat.sp_cholesky(Side::Lower) {
                SparseLinearSolver::Cholesky(chol)
            } else if let Ok(lu) = bpp_mat.sp_lu() {
                SparseLinearSolver::Lu(lu)
            } else {
                return Err("Failed to factorize sparse B'' (both Cholesky and LU failed)");
            };
            Some(solver)
        } else {
            None
        };

        Ok(Self {
            bp_solver,
            bpp_solver,
            non_ref_buses,
            non_ref_map,
            pq_buses,
            pq_map,
            p_spec,
            q_spec,
        })
    }

    /// Solves B' * x = rhs
    #[inline]
    pub fn solve_bp(&self, rhs: &Mat<f64>) -> Mat<f64> {
        self.bp_solver.solve(rhs)
    }

    /// Solves B'' * x = rhs
    #[inline]
    pub fn solve_bpp(&self, rhs: &Mat<f64>) -> Mat<f64> {
        if let Some(solver) = &self.bpp_solver {
            solver.solve(rhs)
        } else {
            Mat::<f64>::zeros(rhs.nrows(), rhs.ncols())
        }
    }
}

/// Fast Decoupled + Woodbury N-2 Contingency Evaluator
pub fn evaluate_n2_woodbury(
    grid: &GridModel,
    base_factors: &BaseFactors,
    warm_v: &[f64],
    warm_theta: &[f64],
    trip1: usize,
    trip2: usize,
) -> ContingencyResult {
    let t_start = Instant::now();
    let br1 = &grid.branches[trip1];
    let br2 = &grid.branches[trip2];

    let mut res = ContingencyResult {
        branch1_idx: trip1,
        branch2_idx: trip2,
        from_bus1: br1.from,
        to_bus1: br1.to,
        from_bus2: br2.from,
        to_bus2: br2.to,
        status: ContingencyStatus::Diverged,
        p_iters: 0,
        q_iters: 0,
        solve_time_us: 0.0,
        max_loading_pct: 0.0,
        overloaded_branch_count: 0,
        min_voltage: 1.0,
        min_voltage_bus: 0,
        max_voltage: 1.0,
        max_voltage_bus: 0,
        voltage_violation_count: 0,
        pi_v: 0.0,
        pi_p: 0.0,
        pi_total: 0.0,
        ploss_mw: 0.0,
        isolated_load_mw: 0.0,
        isolated_bus_count: 0,
    };

    // 1. FAST TOPOLOGY ISLANDING CHECK
    let island_info = check_islanding(grid, trip1, trip2);
    if island_info.is_islanded {
        res.status = ContingencyStatus::Islanded;
        res.isolated_load_mw = island_info.isolated_load_mw;
        res.isolated_bus_count = island_info.isolated_bus_count;
        res.pi_total = 1000.0 + 10.0 * island_info.isolated_load_mw;
        res.solve_time_us = t_start.elapsed().as_secs_f64() * 1_000_000.0;
        return res;
    }

    let n = grid.buses.len();
    let nr_count = base_factors.non_ref_buses.len();

    // 2. BUILD WOODBURY MATRICES FOR B' (P-Theta)
    // U_p has 2 columns: column 0 for branch trip1, column 1 for branch trip2
    let mut u_p = Mat::<f64>::zeros(nr_count, 2);
    let mut cp_inv = [0.0f64; 2]; // diagonal entries of C_p^-1 = [-x1, -x2]

    let f1 = *grid.id_to_idx.get(&br1.from).unwrap();
    let t1 = *grid.id_to_idx.get(&br1.to).unwrap();
    let fi1 = base_factors.non_ref_map[f1];
    let ti1 = base_factors.non_ref_map[t1];
    if let Some(idx) = fi1 {
        u_p[(idx, 0)] = 1.0;
    }
    if let Some(idx) = ti1 {
        u_p[(idx, 0)] = -1.0;
    }
    cp_inv[0] = -br1.x;

    let f2 = *grid.id_to_idx.get(&br2.from).unwrap();
    let t2 = *grid.id_to_idx.get(&br2.to).unwrap();
    let fi2 = base_factors.non_ref_map[f2];
    let ti2 = base_factors.non_ref_map[t2];
    if let Some(idx) = fi2 {
        u_p[(idx, 1)] = 1.0;
    }
    if let Some(idx) = ti2 {
        u_p[(idx, 1)] = -1.0;
    }
    cp_inv[1] = -br2.x;

    // Z_p = B'^-1 * U_p (dimension: nr_count x 2)
    let z_p = base_factors.solve_bp(&u_p);

    // O(1) projection helper: U_p^T * X for col 0 and 1
    let mult_ut = |col: usize, x_mat: &Mat<f64>, x_col: usize| -> f64 {
        let (fi, ti) = if col == 0 { (fi1, ti1) } else { (fi2, ti2) };
        let mut val = 0.0f64;
        if let Some(idx) = fi {
            val += x_mat[(idx, x_col)];
        }
        if let Some(idx) = ti {
            val -= x_mat[(idx, x_col)];
        }
        val
    };

    // Core matrix M_p = C_p^-1 + U_p^T * Z_p (dimension: 2 x 2)
    let mut m_p = [[0.0f64; 2]; 2];
    for i in 0..2 {
        for j in 0..2 {
            let diag = if i == j { cp_inv[i] } else { 0.0 };
            m_p[i][j] = diag + mult_ut(i, &z_p, j);
        }
    }

    // Invert 2x2 matrix M_p:
    let det = m_p[0][0] * m_p[1][1] - m_p[0][1] * m_p[1][0];
    if det.abs() < 1e-9 {
        // Singular post-contingency susceptance matrix: voltage collapse / diverged
        res.status = ContingencyStatus::Diverged;
        res.pi_total = 500.0;
        res.solve_time_us = t_start.elapsed().as_secs_f64() * 1_000_000.0;
        return res;
    }

    let inv_det = 1.0 / det;
    let core_inv = [
        [m_p[1][1] * inv_det, -m_p[0][1] * inv_det],
        [-m_p[1][0] * inv_det, m_p[0][0] * inv_det],
    ];

    // Helper: Woodbury solve for B'_post * dTheta = rhs
    let solve_woodbury_p = |rhs: &Mat<f64>| -> Mat<f64> {
        let d_base = base_factors.solve_bp(rhs);
        // temp = U_p^T * d_base (length 2) in O(1)
        let ut_dbase = [
            mult_ut(0, &d_base, 0),
            mult_ut(1, &d_base, 0),
        ];
        // alpha = CoreInv * ut_dbase
        let alpha0 = core_inv[0][0] * ut_dbase[0] + core_inv[0][1] * ut_dbase[1];
        let alpha1 = core_inv[1][0] * ut_dbase[0] + core_inv[1][1] * ut_dbase[1];

        // result = d_base - Z_p * alpha
        let mut res_mat = Mat::<f64>::zeros(nr_count, 1);
        for k in 0..nr_count {
            res_mat[(k, 0)] = d_base[(k, 0)] - (z_p[(k, 0)] * alpha0 + z_p[(k, 1)] * alpha1);
        }
        res_mat
    };

    // 3. FAST ITERATION SOLVER (WARM-START FROM BASE CASE)
    let mut theta = warm_theta.to_vec();
    let mut v = warm_v.to_vec();
    let ref_idx = grid.ref_bus_idx;
    theta[ref_idx] = 0.0;

    let tol = 1e-3;
    let max_iters = 15;
    let mut converged = false;

    // Constant power injection specifications in p.u. (from BaseFactors)
    let p_spec = &base_factors.p_spec;
    let q_spec = &base_factors.q_spec;

    for _iter in 0..max_iters {
        // Calculate power flow mismatches with remaining branches
        let mut p_calc = vec![0.0f64; n];
        let mut q_calc = vec![0.0f64; n];

        // Bus shunt power
        for i in 0..n {
            p_calc[i] += grid.buses[i].gs / grid.base_mva * v[i] * v[i];
            q_calc[i] += -grid.buses[i].bs / grid.base_mva * v[i] * v[i];
        }

        // Branch power flows
        for (idx, br) in grid.branches.iter().enumerate() {
            if idx == trip1 || idx == trip2 || br.status == 0 {
                continue;
            }
            let bi = *grid.id_to_idx.get(&br.from).unwrap();
            let bj = *grid.id_to_idx.get(&br.to).unwrap();

            let z2 = br.r * br.r + br.x * br.x;
            let g_ij = br.r / z2;
            let b_ij = -br.x / z2;

            let vi = v[bi];
            let vj = v[bj];
            let d_theta = theta[bi] - theta[bj];

            let cos_th = d_theta.cos();
            let sin_th = d_theta.sin();

            // P_ij = Vi^2 * G_ij - Vi * Vj * (G_ij * cos + B_ij * sin)
            let p_from = vi * vi * g_ij - vi * vj * (g_ij * cos_th + b_ij * sin_th);
            let q_from = -vi * vi * (b_ij + br.b / 2.0) - vi * vj * (g_ij * sin_th - b_ij * cos_th);

            let p_to = vj * vj * g_ij - vi * vj * (g_ij * cos_th - b_ij * sin_th);
            let q_to = -vj * vj * (b_ij + br.b / 2.0) - vi * vj * (-g_ij * sin_th - b_ij * cos_th);

            p_calc[bi] += p_from;
            q_calc[bi] += q_from;
            p_calc[bj] += p_to;
            q_calc[bj] += q_to;
        }

        // Compute active power mismatch dP / V
        let mut max_p_mis = 0.0f64;
        let mut mis_p = Mat::<f64>::zeros(nr_count, 1);
        for (i, &bus_idx) in base_factors.non_ref_buses.iter().enumerate() {
            let p_err = (p_calc[bus_idx] - p_spec[bus_idx]) / v[bus_idx];
            mis_p[(i, 0)] = -p_err;
            max_p_mis = max_p_mis.max(p_err.abs());
        }

        // Compute reactive power mismatch dQ / V for PQ buses
        let mut max_q_mis = 0.0f64;
        for &bus_idx in &base_factors.pq_buses {
            let q_err = (q_calc[bus_idx] - q_spec[bus_idx]) / v[bus_idx];
            max_q_mis = max_q_mis.max(q_err.abs());
        }

        if max_p_mis < tol && max_q_mis < tol {
            converged = true;
            break;
        }

        if max_p_mis > 20.0 || max_p_mis.is_nan() {
            converged = false;
            break;
        }

        // P-step: Update angles using Woodbury solver
        let d_va = solve_woodbury_p(&mis_p);
        for (i, &bus_idx) in base_factors.non_ref_buses.iter().enumerate() {
            theta[bus_idx] += d_va[(i, 0)];
        }
        res.p_iters += 1;

        // Q-step: Update PQ voltages using base B''
        if !base_factors.pq_buses.is_empty() {
            let pq_count = base_factors.pq_buses.len();
            let mut mis_q = Mat::<f64>::zeros(pq_count, 1);
            for (i, &bus_idx) in base_factors.pq_buses.iter().enumerate() {
                let q_err = (q_calc[bus_idx] - q_spec[bus_idx]) / v[bus_idx];
                mis_q[(i, 0)] = -q_err;
            }
            let d_vm = base_factors.solve_bpp(&mis_q);
            for (i, &bus_idx) in base_factors.pq_buses.iter().enumerate() {
                v[bus_idx] = (v[bus_idx] + d_vm[(i, 0)]).clamp(0.5, 1.5);
            }
            res.q_iters += 1;
        }
    }

    if !converged {
        res.status = ContingencyStatus::Diverged;
        res.pi_total = 500.0;
        res.solve_time_us = t_start.elapsed().as_secs_f64() * 1_000_000.0;
        return res;
    }

    // 4. POST-PROCESSING SEVERITY INDICES
    res.min_voltage = 999.0;
    res.max_voltage = -999.0;
    let mut pi_v = 0.0;

    for i in 0..n {
        let vi = v[i];
        if vi < res.min_voltage {
            res.min_voltage = vi;
            res.min_voltage_bus = grid.buses[i].id;
        }
        if vi > res.max_voltage {
            res.max_voltage = vi;
            res.max_voltage_bus = grid.buses[i].id;
        }

        let dev = (vi - 1.0).abs() / 0.10;
        pi_v += dev * dev;

        if vi < 0.90 {
            res.voltage_violation_count += 1;
            let viol = (0.90 - vi) / 0.10;
            pi_v += 10.0 * viol.powi(4);
        } else if vi > 1.10 {
            res.voltage_violation_count += 1;
            let viol = (vi - 1.10) / 0.10;
            pi_v += 10.0 * viol.powi(4);
        }
    }

    res.pi_v = pi_v;

    // Evaluate branch loadings
    let mut pi_p = 0.0f64;
    let mut max_loading = 0.0f64;
    let mut overloaded_count = 0;
    let mut total_ploss = 0.0f64;

    for (idx, br) in grid.branches.iter().enumerate() {
        if idx == trip1 || idx == trip2 || br.status == 0 {
            continue;
        }
        let bi = *grid.id_to_idx.get(&br.from).unwrap();
        let bj = *grid.id_to_idx.get(&br.to).unwrap();

        let z2 = br.r * br.r + br.x * br.x;
        let g_ij = br.r / z2;
        let b_ij = -br.x / z2;

        let vi = v[bi];
        let vj = v[bj];
        let d_theta = theta[bi] - theta[bj];

        let cos_th = d_theta.cos();
        let sin_th = d_theta.sin();

        let p_from = vi * vi * g_ij - vi * vj * (g_ij * cos_th + b_ij * sin_th);
        let q_from = -vi * vi * (b_ij + br.b / 2.0) - vi * vj * (g_ij * sin_th - b_ij * cos_th);

        let p_to = vj * vj * g_ij - vi * vj * (g_ij * cos_th - b_ij * sin_th);
        let q_to = -vj * vj * (b_ij + br.b / 2.0) - vi * vj * (-g_ij * sin_th - b_ij * cos_th);

        let s_from = (p_from * p_from + q_from * q_from).sqrt() * grid.base_mva;
        let s_to = (p_to * p_to + q_to * q_to).sqrt() * grid.base_mva;
        let s_max = s_from.max(s_to);

        total_ploss += (p_from + p_to) * grid.base_mva;

        if br.rate_a > 0.0 {
            let load_pct = (s_max / br.rate_a) * 100.0;
            max_loading = max_loading.max(load_pct);
            if load_pct > 100.0 {
                overloaded_count += 1;
                let ratio = s_max / br.rate_a;
                pi_p += ratio.powi(4);
            }
        }
    }

    res.max_loading_pct = max_loading;
    res.overloaded_branch_count = overloaded_count;
    res.pi_p = pi_p;
    res.ploss_mw = total_ploss.max(0.0);
    res.pi_total = res.pi_v + 2.0 * res.pi_p;

    res.status = if overloaded_count > 0 {
        ContingencyStatus::Overload
    } else {
        ContingencyStatus::Stable
    };

    res.solve_time_us = t_start.elapsed().as_secs_f64() * 1_000_000.0;
    res
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_matpower;
    use std::path::Path;

    #[test]
    fn test_sparse_base_factors_all_cases() {
        for case_name in &["case14.m", "case57.m", "case118.m", "case9241pegase.m"] {
            let path = format!("../data/{}", case_name);
            let path_ref = Path::new(&path);
            if !path_ref.exists() {
                continue;
            }
            let t0 = Instant::now();
            let grid = parse_matpower(path_ref).unwrap();
            let factors = BaseFactors::build(&grid).expect("Base factors build failed");
            let build_dur = t0.elapsed();

            let nr = factors.non_ref_buses.len();
            let mut rhs = Mat::<f64>::zeros(nr, 2);
            rhs[(0, 0)] = 1.0;
            rhs[(0, 1)] = -1.0;

            let t_solve = Instant::now();
            let x = factors.solve_bp(&rhs);
            let solve_dur = t_solve.elapsed();
            assert_eq!(x.nrows(), nr);

            println!("✓ BaseFactors on {} (N = {}): Build: {:.2} ms, 2-col Solve: {:.3} ms",
                case_name, nr, build_dur.as_secs_f64() * 1000.0, solve_dur.as_secs_f64() * 1000.0);
        }
    }

    #[test]
    fn test_base_factors_case14() {
        let path = Path::new("../data/case14.m");
        if path.exists() {
            let grid = parse_matpower(path).expect("Should parse case14");
            let base_factors = BaseFactors::build(&grid).expect("Base factors should build");
            assert_eq!(base_factors.non_ref_buses.len(), 13);
            assert_eq!(base_factors.pq_buses.len(), 9);

            let warm_v = vec![1.0; 14];
            let warm_theta = vec![0.0; 14];

            // Evaluate N-2 tripping branch 0 and branch 2
            let res = evaluate_n2_woodbury(&grid, &base_factors, &warm_v, &warm_theta, 0, 2);
            assert_eq!(res.status, ContingencyStatus::Stable);
            assert!(res.solve_time_us > 0.0);
            println!("✓ N-2 evaluate (0, 2) completed in {:.2} µs: Max loading: {:.1}%, PI_Total: {:.2}",
                res.solve_time_us, res.max_loading_pct, res.pi_total);
        }
    }
}
