use clap::Parser;
use rayon::prelude::*;
use serde::Serialize;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;
use std::time::Instant;

use gridguard::grid::{ContingencyResult, ContingencyStatus};
use gridguard::parser::parse_matpower;
use gridguard::power_flow::solve_base_case;
use gridguard::woodbury::{evaluate_n2_woodbury, BaseFactors};

#[derive(Parser, Debug)]
#[command(author, version, about = "GridGuard N-2 Rust Engine: Ultra-Fast Sherman-Morrison-Woodbury Contingency Screener", long_about = None)]
struct Args {
    /// Path to MATPOWER case file (.m)
    #[arg(value_name = "CASE_FILE")]
    case_file: PathBuf,

    /// Number of worker threads
    #[arg(short, long, default_value_t = 0)]
    threads: usize,

    /// Limit maximum number of contingencies to evaluate
    #[arg(short, long, default_value_t = 0)]
    limit: usize,

    /// Force simulate all possible N-2 pairs regardless of grid size
    #[arg(short, long, default_value_t = false)]
    all: bool,

    /// Number of top critical contingencies to display
    #[arg(short, long, default_value_t = 15)]
    top: usize,

    /// Export simulation results to JSON file
    #[arg(long, value_name = "PATH")]
    json_out: Option<PathBuf>,
}

#[derive(Serialize)]
struct TopContingencyJson {
    rank: usize,
    branch_pair: String,
    from_to_1: String,
    from_to_2: String,
    status: String,
    max_loading_pct: f64,
    overloaded_branches: usize,
    voltage_violations: usize,
    min_voltage_pu: f64,
    min_voltage_bus: i32,
    pi_v: f64,
    pi_p: f64,
    pi_total: f64,
    ploss_mw: f64,
    solve_time_us: f64,
}

#[derive(Serialize)]
struct PerformanceJson {
    total_contingencies_possible: usize,
    contingencies_simulated: usize,
    threads_used: usize,
    wall_clock_time_ms: f64,
    contingency_throughput_per_sec: f64,
    avg_solve_time_us: f64,
    min_solve_time_us: f64,
    max_solve_time_us: f64,
}

#[derive(Serialize)]
struct StatusSummaryJson {
    converged_count: usize,
    overloaded_count: usize,
    islanded_count: usize,
    diverged_count: usize,
}

#[derive(Serialize)]
struct ReportJson {
    engine: String,
    case_file: String,
    num_buses: usize,
    num_branches: usize,
    total_load_mw: f64,
    base_ploss_mw: f64,
    performance: PerformanceJson,
    status_summary: StatusSummaryJson,
    top_critical_contingencies: Vec<TopContingencyJson>,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    let threads = if args.threads > 0 {
        args.threads
    } else {
        std::thread::available_parallelism()
            .map(|n| n.get())
            .unwrap_or(4)
    };

    rayon::ThreadPoolBuilder::new()
        .num_threads(threads)
        .build_global()
        .ok();

    println!("================================================================================");
    println!("|   RUST HIGH-PERFORMANCE N-2 CONTINGENCY SCREENING ENGINE                     |");
    println!("|   Sherman-Morrison-Woodbury Rank-2 Inversion & Multi-Island Detection        |");
    println!("================================================================================");

    println!("Memuat kasus grid: {}...", args.case_file.display());
    let grid = parse_matpower(&args.case_file)?;
    println!(
        "✓ Kasus {} berhasil dimuat: {} bus, {} saluran transmisi, {} generator.",
        args.case_file.file_name().unwrap_or_default().to_string_lossy(),
        grid.buses.len(),
        grid.branches.len(),
        grid.generators.len()
    );

    println!("Membangun matriks dasar B' & B'' serta melakukan faktorisasi Cholesky...");
    let base_factors = BaseFactors::build(&grid)?;

    println!("Menjalankan Base-Case Power Flow (Warm-Start Initialization)...");
    let base_res = solve_base_case(&grid, &base_factors);
    if !base_res.converged {
        eprintln!("⚠️ Base Case Power Flow Gagal Konvergen! Periksa integritas data grid.");
        std::process::exit(1);
    }
    println!(
        "✓ Base Case Selesai: Ploss = {:.3} MW, Total Beban: {:.2} MW.",
        base_res.base_ploss_mw, grid.total_load_mw
    );

    let m = grid.branches.len();
    let total_possible = m * (m - 1) / 2;
    println!("Total kombinasi pasangan N-2: {} skenario.", total_possible);

    let mut pairs_to_simulate = Vec::new();
    let target_count = if !args.all && args.limit > 0 {
        args.limit.min(total_possible)
    } else if !args.all && total_possible > 100_000 {
        println!("💡 Catatan: Sistem besar ({} skenario). Menjalankan 10,000 skenario pertama (Gunakan --all untuk seluruhnya).", total_possible);
        10_000.min(total_possible)
    } else {
        total_possible
    };

    pairs_to_simulate.reserve(target_count);
    let mut count = 0;
    'outer: for i in 0..m {
        for j in (i + 1)..m {
            pairs_to_simulate.push((i, j));
            count += 1;
            if count >= target_count {
                break 'outer;
            }
        }
    }

    println!(
        "\n[Menjalankan Simulasi {} Kontingensi N-2 dengan {} Threads CPU Rayon]...",
        pairs_to_simulate.len(),
        threads
    );

    let t_start_sim = Instant::now();

    // Parallel simulation with Rayon
    let mut results: Vec<ContingencyResult> = pairs_to_simulate
        .par_iter()
        .map(|&(trip1, trip2)| {
            evaluate_n2_woodbury(
                &grid,
                &base_factors,
                &base_res.warm_v,
                &base_res.warm_theta,
                trip1,
                trip2,
            )
        })
        .collect();

    let sim_elapsed = t_start_sim.elapsed();
    let wall_clock_ms = sim_elapsed.as_secs_f64() * 1000.0;
    let throughput = (results.len() as f64) / sim_elapsed.as_secs_f64();

    // Sort by composite severity PI_Total descending
    results.sort_by(|a, b| b.pi_total.partial_cmp(&a.pi_total).unwrap_or(std::cmp::Ordering::Equal));

    let mut converged_cnt = 0;
    let mut overload_cnt = 0;
    let mut islanded_cnt = 0;
    let mut diverged_cnt = 0;
    let mut total_solve_time = 0.0f64;
    let mut min_solve_time = f64::MAX;
    let mut max_solve_time = 0.0f64;

    for r in &results {
        match r.status {
            ContingencyStatus::Stable => converged_cnt += 1,
            ContingencyStatus::Overload => overload_cnt += 1,
            ContingencyStatus::Islanded => islanded_cnt += 1,
            ContingencyStatus::Diverged => diverged_cnt += 1,
        }
        total_solve_time += r.solve_time_us;
        min_solve_time = min_solve_time.min(r.solve_time_us);
        max_solve_time = max_solve_time.max(r.solve_time_us);
    }

    let avg_solve_time = if !results.is_empty() {
        total_solve_time / (results.len() as f64)
    } else {
        0.0
    };

    println!("\n================================================================================");
    println!("|                   HASIL SKRINING KONTINGENSI N-2 (RUST ENGINE)               |");
    println!("================================================================================");
    println!("Total Pasangan Dievaluasi: {} skenario", results.len());
    println!("Wall-Clock Time          : {:.2} ms", wall_clock_ms);
    println!("Throughput Komputasi     : {:.1} kontingensi / detik", throughput);
    println!("Rata-rata Waktu / Kasus  : {:.2} µs (Min: {:.2} µs, Max: {:.2} µs)", avg_solve_time, min_solve_time, max_solve_time);
    println!("Status Ringkasan         : {} Stabil, {} Overload, {} Islanding, {} Divergen", converged_cnt, overload_cnt, islanded_cnt, diverged_cnt);
    println!("--------------------------------------------------------------------------------");

    println!("\n+------+--------------------+--------------------+--------+------------+------------+------------+");
    println!("| Rank | Pasangan Saluran   | Bus Terminal       | Status | Max Load % | PI Total   | Ploss (MW) |");
    println!("+------+--------------------+--------------------+--------+------------+------------+------------+");

    let top_k = args.top.min(results.len());
    for (i, r) in results.iter().take(top_k).enumerate() {
        let pair_str = format!("#{} & #{}", r.branch1_idx + 1, r.branch2_idx + 1);
        let bus_str = format!("{}-{} & {}-{}", r.from_bus1, r.to_bus1, r.from_bus2, r.to_bus2);
        println!(
            "| {:<4} | {:<18} | {:<18} | {:<6} | {:>10.1}% | {:>10.2} | {:>10.2} |",
            i + 1,
            pair_str,
            bus_str,
            r.status.as_str(),
            r.max_loading_pct,
            r.pi_total,
            r.ploss_mw
        );
    }
    println!("+------+--------------------+--------------------+--------+------------+------------+------------+");

    if let Some(json_path) = args.json_out {
        let top_json: Vec<TopContingencyJson> = results
            .iter()
            .take(50.max(args.top))
            .enumerate()
            .map(|(i, r)| TopContingencyJson {
                rank: i + 1,
                branch_pair: format!("#{} & #{}", r.branch1_idx + 1, r.branch2_idx + 1),
                from_to_1: format!("{}-{}", r.from_bus1, r.to_bus1),
                from_to_2: format!("{}-{}", r.from_bus2, r.to_bus2),
                status: r.status.as_str().to_string(),
                max_loading_pct: r.max_loading_pct,
                overloaded_branches: r.overloaded_branch_count,
                voltage_violations: r.voltage_violation_count,
                min_voltage_pu: r.min_voltage,
                min_voltage_bus: r.min_voltage_bus,
                pi_v: r.pi_v,
                pi_p: r.pi_p,
                pi_total: r.pi_total,
                ploss_mw: r.ploss_mw,
                solve_time_us: r.solve_time_us,
            })
            .collect();

        let report = ReportJson {
            engine: "GridGuard C++20 / Rust Woodbury Engine".to_string(),
            case_file: args.case_file.display().to_string(),
            num_buses: grid.buses.len(),
            num_branches: grid.branches.len(),
            total_load_mw: grid.total_load_mw,
            base_ploss_mw: base_res.base_ploss_mw,
            performance: PerformanceJson {
                total_contingencies_possible: total_possible,
                contingencies_simulated: results.len(),
                threads_used: threads,
                wall_clock_time_ms: wall_clock_ms,
                contingency_throughput_per_sec: throughput,
                avg_solve_time_us: avg_solve_time,
                min_solve_time_us: min_solve_time,
                max_solve_time_us: max_solve_time,
            },
            status_summary: StatusSummaryJson {
                converged_count: converged_cnt,
                overloaded_count: overload_cnt,
                islanded_count: islanded_cnt,
                diverged_count: diverged_cnt,
            },
            top_critical_contingencies: top_json,
        };

        let mut f = File::create(&json_path)?;
        let json_str = serde_json::to_string_pretty(&report)?;
        f.write_all(json_str.as_bytes())?;
        println!("\n💾 Hasil simulasi lengkap berhasil diekspor ke: {}", json_path.display());
    }

    Ok(())
}
