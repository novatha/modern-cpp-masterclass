use std::collections::HashMap;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Bus {
    pub id: i32,
    pub bus_type: i32, // 1 = PQ, 2 = PV, 3 = Ref/Slack
    pub pd: f64,       // MW load
    pub qd: f64,       // MVAr load
    pub gs: f64,       // MW shunt at V = 1.0
    pub bs: f64,       // MVAr shunt at V = 1.0
    pub vm: f64,       // Voltage magnitude (p.u.)
    pub va: f64,       // Voltage angle (rad)
    pub pg: f64,       // MW generation
    pub qg: f64,       // MVAr generation
    pub vg: f64,       // Voltage setpoint
}

impl Default for Bus {
    fn default() -> Self {
        Self {
            id: 0,
            bus_type: 1,
            pd: 0.0,
            qd: 0.0,
            gs: 0.0,
            bs: 0.0,
            vm: 1.0,
            va: 0.0,
            pg: 0.0,
            qg: 0.0,
            vg: 1.0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Branch {
    pub id: usize,     // 1-based index
    pub from: i32,     // From bus ID
    pub to: i32,       // To bus ID
    pub from_idx: usize, // 0-based index into buses array
    pub to_idx: usize,   // 0-based index into buses array
    pub r: f64,        // Resistance (p.u.)
    pub x: f64,        // Reactance (p.u.)
    pub b: f64,        // Total line charging susceptance (p.u.)
    pub rate_a: f64,   // MVA rating (0.0 = unlimited)
    pub ratio: f64,    // Transformer tap ratio (1.0 for lines)
    pub angle: f64,    // Transformer phase shift angle (rad)
    pub status: i32,   // 1 = in-service, 0 = out-of-service
}

impl Default for Branch {
    fn default() -> Self {
        Self {
            id: 0,
            from: 0,
            to: 0,
            from_idx: 0,
            to_idx: 0,
            r: 0.0,
            x: 0.0,
            b: 0.0,
            rate_a: 0.0,
            ratio: 1.0,
            angle: 0.0,
            status: 1,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Generator {
    pub bus: i32,
    pub pg: f64,
    pub qg: f64,
    pub qmax: f64,
    pub qmin: f64,
    pub vg: f64,
    pub mbase: f64,
    pub status: i32,
    pub pmax: f64,
    pub pmin: f64,
}

impl Default for Generator {
    fn default() -> Self {
        Self {
            bus: 0,
            pg: 0.0,
            qg: 0.0,
            qmax: 0.0,
            qmin: 0.0,
            vg: 1.0,
            mbase: 100.0,
            status: 1,
            pmax: 0.0,
            pmin: 0.0,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ContingencyStatus {
    Stable,
    Overload,
    Islanded,
    Diverged,
}

impl ContingencyStatus {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Stable => "STABLE",
            Self::Overload => "OVERLOAD",
            Self::Islanded => "ISLANDED",
            Self::Diverged => "DIVERGED",
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContingencyResult {
    pub branch1_idx: usize,
    pub branch2_idx: usize,
    pub from_bus1: i32,
    pub to_bus1: i32,
    pub from_bus2: i32,
    pub to_bus2: i32,
    pub status: ContingencyStatus,
    pub p_iters: usize,
    pub q_iters: usize,
    pub solve_time_us: f64,
    pub max_loading_pct: f64,
    pub overloaded_branch_count: usize,
    pub min_voltage: f64,
    pub min_voltage_bus: i32,
    pub max_voltage: f64,
    pub max_voltage_bus: i32,
    pub voltage_violation_count: usize,
    pub pi_v: f64,
    pub pi_p: f64,
    pub pi_total: f64,
    pub ploss_mw: f64,
    pub isolated_load_mw: f64,
    pub isolated_bus_count: usize,
}

#[derive(Debug, Clone)]
pub struct GridModel {
    pub base_mva: f64,
    pub buses: Vec<Bus>,
    pub branches: Vec<Branch>,
    pub generators: Vec<Generator>,
    pub id_to_idx: HashMap<i32, usize>,
    pub ref_bus_idx: usize,
    pub total_load_mw: f64,
    pub total_gen_mw: f64,
}

impl GridModel {
    pub fn new() -> Self {
        Self {
            base_mva: 100.0,
            buses: Vec::new(),
            branches: Vec::new(),
            generators: Vec::new(),
            id_to_idx: HashMap::new(),
            ref_bus_idx: 0,
            total_load_mw: 0.0,
            total_gen_mw: 0.0,
        }
    }

    pub fn rebuild_index_and_stats(&mut self) {
        self.id_to_idx.clear();
        self.total_load_mw = 0.0;
        self.total_gen_mw = 0.0;
        self.ref_bus_idx = 0;

        for (idx, bus) in self.buses.iter().enumerate() {
            self.id_to_idx.insert(bus.id, idx);
            self.total_load_mw += bus.pd;
            if bus.bus_type == 3 {
                self.ref_bus_idx = idx;
            }
        }

        for g in &self.generators {
            if g.status > 0 {
                self.total_gen_mw += g.pg;
            }
        }

        for br in &mut self.branches {
            br.from_idx = *self.id_to_idx.get(&br.from).unwrap_or(&0);
            br.to_idx = *self.id_to_idx.get(&br.to).unwrap_or(&0);
        }
    }
}
