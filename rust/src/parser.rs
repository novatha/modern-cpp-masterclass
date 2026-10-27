use std::f64::consts::PI;
use std::fs::File;
use std::io::{self, BufRead, BufReader};
use std::path::Path;

use crate::grid::{Branch, Bus, Generator, GridModel};

pub fn parse_matpower<P: AsRef<Path>>(filepath: P) -> io::Result<GridModel> {
    let file = File::open(filepath)?;
    let reader = BufReader::new(file);

    let mut grid = GridModel::new();

    #[derive(PartialEq, Eq)]
    enum Section {
        None,
        Bus,
        Gen,
        Branch,
    }

    let mut current_section = Section::None;
    let mut branch_id_counter = 1usize;

    for line_result in reader.lines() {
        let line = line_result?;
        let trimmed = line.trim();

        if trimmed.is_empty() || trimmed.starts_with('%') {
            continue;
        }

        // Check for baseMVA
        if trimmed.contains("mpc.baseMVA") {
            if let Some(eq_idx) = trimmed.find('=') {
                let after_eq = &trimmed[eq_idx + 1..];
                let val_str = after_eq.trim_matches(|c: char| c.is_whitespace() || c == ';');
                if let Ok(val) = val_str.parse::<f64>() {
                    grid.base_mva = val;
                }
            }
            continue;
        }

        // Section header transitions
        if trimmed.contains("mpc.bus") && trimmed.contains('[') {
            current_section = Section::Bus;
            continue;
        } else if trimmed.contains("mpc.gen") && !trimmed.contains("gencost") && trimmed.contains('[') {
            current_section = Section::Gen;
            continue;
        } else if trimmed.contains("mpc.branch") && trimmed.contains('[') {
            current_section = Section::Branch;
            continue;
        }

        if trimmed.contains("];") || trimmed == "]" {
            current_section = Section::None;
            continue;
        }

        // Parse matrix row
        // Strip trailing comment '% ...'
        let clean_row = if let Some(pct_idx) = trimmed.find('%') {
            &trimmed[..pct_idx]
        } else {
            trimmed
        };

        let tokens: Vec<&str> = clean_row
            .trim_matches(|c: char| c == ';' || c.is_whitespace())
            .split_whitespace()
            .collect();

        if tokens.is_empty() {
            continue;
        }

        match current_section {
            Section::Bus => {
                if tokens.len() >= 4 {
                    let mut bus = Bus::default();
                    bus.id = tokens[0].parse().unwrap_or(0);
                    bus.bus_type = tokens[1].parse().unwrap_or(1);
                    bus.pd = tokens[2].parse().unwrap_or(0.0);
                    bus.qd = tokens[3].parse().unwrap_or(0.0);
                    if tokens.len() > 4 {
                        bus.gs = tokens[4].parse().unwrap_or(0.0);
                    }
                    if tokens.len() > 5 {
                        bus.bs = tokens[5].parse().unwrap_or(0.0);
                    }
                    if tokens.len() > 7 {
                        bus.vm = tokens[7].parse().unwrap_or(1.0);
                    }
                    if tokens.len() > 8 {
                        let va_deg: f64 = tokens[8].parse().unwrap_or(0.0);
                        bus.va = va_deg * PI / 180.0;
                    }
                    grid.buses.push(bus);
                }
            }
            Section::Gen => {
                if tokens.len() >= 4 {
                    let mut generator = Generator::default();
                    generator.bus = tokens[0].parse().unwrap_or(0);
                    generator.pg = tokens[1].parse().unwrap_or(0.0);
                    generator.qg = tokens[2].parse().unwrap_or(0.0);
                    if tokens.len() > 3 {
                        generator.qmax = tokens[3].parse().unwrap_or(0.0);
                    }
                    if tokens.len() > 4 {
                        generator.qmin = tokens[4].parse().unwrap_or(0.0);
                    }
                    if tokens.len() > 5 {
                        generator.vg = tokens[5].parse().unwrap_or(1.0);
                    }
                    if tokens.len() > 6 {
                        generator.mbase = tokens[6].parse().unwrap_or(100.0);
                    }
                    if tokens.len() > 7 {
                        generator.status = tokens[7].parse().unwrap_or(1);
                    }
                    if tokens.len() > 8 {
                        generator.pmax = tokens[8].parse().unwrap_or(0.0);
                    }
                    if tokens.len() > 9 {
                        generator.pmin = tokens[9].parse().unwrap_or(0.0);
                    }
                    grid.generators.push(generator);
                }
            }
            Section::Branch => {
                if tokens.len() >= 5 {
                    let mut br = Branch::default();
                    br.id = branch_id_counter;
                    branch_id_counter += 1;
                    br.from = tokens[0].parse().unwrap_or(0);
                    br.to = tokens[1].parse().unwrap_or(0);
                    br.r = tokens[2].parse().unwrap_or(0.0);
                    br.x = tokens[3].parse().unwrap_or(0.0);
                    br.b = tokens[4].parse().unwrap_or(0.0);
                    if tokens.len() > 5 {
                        br.rate_a = tokens[5].parse().unwrap_or(0.0);
                    }
                    if tokens.len() > 8 {
                        let tap: f64 = tokens[8].parse().unwrap_or(1.0);
                        br.ratio = if tap == 0.0 { 1.0 } else { tap };
                    }
                    if tokens.len() > 9 {
                        let shift_deg: f64 = tokens[9].parse().unwrap_or(0.0);
                        br.angle = shift_deg * PI / 180.0;
                    }
                    if tokens.len() > 10 {
                        br.status = tokens[10].parse().unwrap_or(1);
                    }
                    grid.branches.push(br);
                }
            }
            Section::None => {}
        }
    }

    grid.rebuild_index_and_stats();

    // Map generator setpoints back into bus structures (matching MATPOWER convention)
    for g in &grid.generators {
        if g.status > 0 {
            if let Some(&b_idx) = grid.id_to_idx.get(&g.bus) {
                grid.buses[b_idx].pg += g.pg;
                grid.buses[b_idx].qg += g.qg;
                grid.buses[b_idx].vg = g.vg;
                if grid.buses[b_idx].bus_type == 1 {
                    grid.buses[b_idx].bus_type = 2; // PQ promoted to PV
                }
            }
        }
    }

    Ok(grid)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_case14() {
        let path = Path::new("../data/case14.m");
        if path.exists() {
            let grid = parse_matpower(path).expect("Should parse case14.m");
            assert_eq!(grid.buses.len(), 14);
            assert_eq!(grid.branches.len(), 20);
            assert_eq!(grid.generators.len(), 5);
            assert!((grid.total_load_mw - 259.0).abs() < 1e-4);
            println!("✓ case14 parsed: {} buses, {} branches, Total Load: {:.2} MW",
                grid.buses.len(), grid.branches.len(), grid.total_load_mw);
        }
    }

    #[test]
    fn test_parse_case57() {
        let path = Path::new("../data/case57.m");
        if path.exists() {
            let grid = parse_matpower(path).expect("Should parse case57.m");
            assert_eq!(grid.buses.len(), 57);
            assert_eq!(grid.branches.len(), 80);
            println!("✓ case57 parsed: {} buses, {} branches, Total Load: {:.2} MW",
                grid.buses.len(), grid.branches.len(), grid.total_load_mw);
        }
    }

    #[test]
    fn test_parse_case118() {
        let path = Path::new("../data/case118.m");
        if path.exists() {
            let grid = parse_matpower(path).expect("Should parse case118.m");
            assert_eq!(grid.buses.len(), 118);
            assert_eq!(grid.branches.len(), 186);
            println!("✓ case118 parsed: {} buses, {} branches, Total Load: {:.2} MW",
                grid.buses.len(), grid.branches.len(), grid.total_load_mw);
        }
    }
}
