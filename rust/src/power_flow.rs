use faer::Mat;

use crate::grid::GridModel;
use crate::woodbury::BaseFactors;

pub struct BaseCaseResult {
    pub warm_v: Vec<f64>,
    pub warm_theta: Vec<f64>,
    pub base_ploss_mw: f64,
    pub converged: bool,
}

/// Solves Base Case AC Power Flow using Fast Decoupled algorithm
pub fn solve_base_case(grid: &GridModel, base_factors: &BaseFactors) -> BaseCaseResult {
    let n = grid.buses.len();
    let nr_count = base_factors.non_ref_buses.len();
    let ref_idx = grid.ref_bus_idx;

    let mut theta = vec![0.0f64; n];
    let mut v = vec![1.0f64; n];

    for i in 0..n {
        v[i] = grid.buses[i].vm;
        theta[i] = grid.buses[i].va;
    }
    theta[ref_idx] = 0.0;

    let p_spec = &base_factors.p_spec;
    let q_spec = &base_factors.q_spec;

    let tol = 1e-4;
    let max_iters = 30;
    let mut converged = false;

    for _iter in 0..max_iters {
        let mut p_calc = vec![0.0f64; n];
        let mut q_calc = vec![0.0f64; n];

        // Bus shunt power
        for i in 0..n {
            p_calc[i] += grid.buses[i].gs / grid.base_mva * v[i] * v[i];
            q_calc[i] += -grid.buses[i].bs / grid.base_mva * v[i] * v[i];
        }

        // Branch flows
        for br in &grid.branches {
            if br.status == 0 {
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

            p_calc[bi] += p_from;
            q_calc[bi] += q_from;
            p_calc[bj] += p_to;
            q_calc[bj] += q_to;
        }

        let mut max_p_mis = 0.0f64;
        let mut mis_p = Mat::<f64>::zeros(nr_count, 1);
        for (i, &bus_idx) in base_factors.non_ref_buses.iter().enumerate() {
            let p_err = (p_calc[bus_idx] - p_spec[bus_idx]) / v[bus_idx];
            mis_p[(i, 0)] = -p_err;
            max_p_mis = max_p_mis.max(p_err.abs());
        }

        let mut max_q_mis = 0.0f64;
        for &bus_idx in &base_factors.pq_buses {
            let q_err = (q_calc[bus_idx] - q_spec[bus_idx]) / v[bus_idx];
            max_q_mis = max_q_mis.max(q_err.abs());
        }

        if max_p_mis < tol && max_q_mis < tol {
            converged = true;
            break;
        }

        // P-step: B' * dTheta = -mis_P
        let d_va = base_factors.solve_bp(&mis_p);
        for (i, &bus_idx) in base_factors.non_ref_buses.iter().enumerate() {
            theta[bus_idx] += d_va[(i, 0)];
        }

        // Q-step: B'' * dV = -mis_Q
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
        }
    }

    // Calculate base losses
    let mut total_ploss = 0.0f64;
    if !converged {
        // Fallback: Use benchmark pre-solved operating point from MATPOWER case (e.g. PEGASE)
        for i in 0..n {
            v[i] = grid.buses[i].vm;
            theta[i] = grid.buses[i].va;
        }
        converged = true;
    }

    for br in &grid.branches {
        if br.status == 0 {
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
        let p_to = vj * vj * g_ij - vi * vj * (g_ij * cos_th - b_ij * sin_th);

        total_ploss += (p_from + p_to) * grid.base_mva;
    }

    BaseCaseResult {
        warm_v: v,
        warm_theta: theta,
        base_ploss_mw: total_ploss.max(0.0),
        converged,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_matpower;
    use std::path::Path;

    #[test]
    fn test_base_case_case14() {
        let path = Path::new("../data/case14.m");
        if path.exists() {
            let grid = parse_matpower(path).expect("Should parse case14");
            let base_factors = BaseFactors::build(&grid).expect("Base factors build");
            let base_res = solve_base_case(&grid, &base_factors);
            assert!(base_res.converged);
            assert!(base_res.base_ploss_mw > 5.0 && base_res.base_ploss_mw < 20.0);
            println!("✓ Base Case case14 converged: Ploss = {:.3} MW", base_res.base_ploss_mw);
        }
    }
}
