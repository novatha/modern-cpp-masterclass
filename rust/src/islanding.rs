use crate::grid::GridModel;

#[derive(Debug, Clone)]
pub struct Dsu {
    parent: Vec<usize>,
    rank: Vec<usize>,
}

impl Dsu {
    pub fn new(n: usize) -> Self {
        Self {
            parent: (0..n).collect(),
            rank: vec![0; n],
        }
    }

    pub fn find(&mut self, i: usize) -> usize {
        let mut root = i;
        while root != self.parent[root] {
            root = self.parent[root];
        }
        let mut curr = i;
        while curr != root {
            let next = self.parent[curr];
            self.parent[curr] = root;
            curr = next;
        }
        root
    }

    pub fn union(&mut self, i: usize, j: usize) -> bool {
        let root_i = self.find(i);
        let root_j = self.find(j);
        if root_i == root_j {
            return false;
        }
        if self.rank[root_i] < self.rank[root_j] {
            self.parent[root_i] = root_j;
        } else if self.rank[root_i] > self.rank[root_j] {
            self.parent[root_j] = root_i;
        } else {
            self.parent[root_j] = root_i;
            self.rank[root_i] += 1;
        }
        true
    }
}

#[derive(Debug, Clone)]
pub struct IslandInfo {
    pub is_islanded: bool,
    pub num_islands: usize,
    pub isolated_load_mw: f64,
    pub isolated_bus_count: usize,
}

pub fn check_islanding(
    grid: &GridModel,
    trip_branch1: usize,
    trip_branch2: usize,
) -> IslandInfo {
    let n = grid.buses.len();
    let mut dsu = Dsu::new(n);

    for (idx, br) in grid.branches.iter().enumerate() {
        if idx == trip_branch1 || idx == trip_branch2 || br.status == 0 {
            continue;
        }
        if let (Some(&u), Some(&v)) = (grid.id_to_idx.get(&br.from), grid.id_to_idx.get(&br.to)) {
            dsu.union(u, v);
        }
    }

    let mut component_sizes = vec![0usize; n];
    let mut component_load = vec![0.0f64; n];
    let mut component_gen = vec![0.0f64; n];

    for i in 0..n {
        let root = dsu.find(i);
        component_sizes[root] += 1;
        component_load[root] += grid.buses[i].pd;
        component_gen[root] += grid.buses[i].pg;
    }

    let ref_root = dsu.find(grid.ref_bus_idx);

    let mut num_islands = 0;
    let mut isolated_load_mw = 0.0;
    let mut isolated_bus_count = 0;

    for i in 0..n {
        if component_sizes[i] > 0 {
            num_islands += 1;
            if i != ref_root {
                isolated_load_mw += component_load[i];
                isolated_bus_count += component_sizes[i];
            }
        }
    }

    IslandInfo {
        is_islanded: num_islands > 1,
        num_islands,
        isolated_load_mw,
        isolated_bus_count,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_matpower;
    use std::path::Path;

    #[test]
    fn test_islanding_case14() {
        let path = Path::new("../data/case14.m");
        if path.exists() {
            let grid = parse_matpower(path).expect("Should parse case14");
            
            // Connected test: trip branch 0 (1-2) and branch 2 (2-3)
            // (Note: tripping 0 and 1 isolates bus 1 because bus 1 only connects to 2 and 5)
            let info_conn = check_islanding(&grid, 0, 2);
            assert_eq!(info_conn.num_islands, 1);
            assert!(!info_conn.is_islanded);

            // In case14, bus 14 is connected by:
            // branch idx 16 (9-14) and branch idx 19 (13-14)
            let info_island = check_islanding(&grid, 16, 19);
            assert!(info_island.is_islanded);
            assert_eq!(info_island.num_islands, 2);
            assert_eq!(info_island.isolated_bus_count, 1);
            assert!((info_island.isolated_load_mw - 14.9).abs() < 1e-4);
            println!("✓ Islanding correctly detected on case14: {} islands, isolated load: {:.2} MW",
                info_island.num_islands, info_island.isolated_load_mw);
        }
    }
}
