#!/usr/bin/env python3
"""
Generate publication-quality IEEE Transactions figures for:
"Ultra-Fast Exhaustive N-2 Contingency Screening in Large-Scale Power Grids 
 via Lock-Free Parallel C++20 and Cholesky-Factored Fast Decoupled AC Power Flow"
"""

import matplotlib.pyplot as plt
import numpy as np
import os

# Set publication style
plt.rcParams.update({
    'font.family': 'serif',
    'font.serif': ['Times New Roman', 'DejaVu Serif', 'Times'],
    'font.size': 9,
    'axes.labelsize': 10,
    'axes.titlesize': 10,
    'xtick.labelsize': 8.5,
    'ytick.labelsize': 8.5,
    'legend.fontsize': 8.5,
    'figure.titlesize': 11,
    'mathtext.fontset': 'stix',
    'lines.linewidth': 1.5,
    'lines.markersize': 5,
    'grid.alpha': 0.4,
    'grid.linestyle': '--'
})

os.makedirs('assets', exist_ok=True)

# ==============================================================================
# Figure 1: Engine Benchmark & Scalability (Log-scale runtime comparison)
# ==============================================================================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.2, 2.8), constrained_layout=True)

engines = [
    'PowerModels.jl\n(Julia Ipopt)',
    'MATPOWER 7.1\n(GNU Octave)',
    'Pandapower 3.5\n(Python SciPy)',
    'C++ NR\n(Eigen SparseLU)',
    'C++ NR\n(SuiteSparse KLU)',
    'LightSim2grid\n(C++ KLU)',
    'Proposed C++20\n(FDXB SimplicialLDLT)'
]

per_solve_ms = [532.15, 134.66, 33.57, 15.78, 8.97, 3.23, 2.75]
colors = ['#888888', '#999999', '#aaaaaa', '#777777', '#555555', '#333333', '#0055aa']

bars = ax1.barh(engines, per_solve_ms, color=colors, edgecolor='black', height=0.65)
ax1.set_xscale('log')
ax1.set_xlabel('Mean Solve Time per AC Flow (ms) [Log Scale]')
ax1.set_title('(a) Single AC Power Flow (PEGASE 1354)', fontweight='bold')
ax1.grid(True, which='both', axis='x')

# Value annotations
for bar, val in zip(bars, per_solve_ms):
    ax1.text(val * 1.15, bar.get_y() + bar.get_height()/2, 
             f'{val:.2f} ms' if val >= 1 else f'{val:.2f} ms', 
             va='center', fontsize=7.5, fontweight='bold' if 'Proposed' in str(bar) else 'normal')
ax1.set_xlim(1.0, 1500)

# (b) Full 1.98M N-2 Screening Wall-Clock Time (Hours/Minutes)
engines_n2 = [
    'PowerModels.jl',
    'MATPOWER (Octave)',
    'Pandapower',
    'LightSim2grid',
    'Proposed C++20 (10-Core)'
]
times_hours = [12.2 * 24, 74.1, 18.5, 1.77, 253.43 / 3600.0] # in hours
time_labels = ['12.2 days', '74.1 hours (3.1 d)', '18.5 hours', '1.77 hours', '4.23 min (253 s)']
colors_n2 = ['#888888', '#999999', '#aaaaaa', '#444444', '#0055aa']

bars2 = ax2.barh(engines_n2, times_hours, color=colors_n2, edgecolor='black', height=0.6)
ax2.set_xscale('log')
ax2.set_xlabel('Total Time for 1.98 Million N-2 Outages [Log Scale]')
ax2.set_title('(b) Exhaustive N-2 Contingency Screening', fontweight='bold')
ax2.grid(True, which='both', axis='x')

for bar, label, h in zip(bars2, time_labels, times_hours):
    ax2.text(h * 1.3, bar.get_y() + bar.get_height()/2, label, 
             va='center', fontsize=7.5, fontweight='bold' if 'Proposed' in label else 'normal')
ax2.set_xlim(0.03, 1000)

plt.savefig('assets/fig_engine_comparison.png', dpi=600)
plt.close()
print("Generated assets/fig_engine_comparison.png")

# ==============================================================================
# Figure 2: Multicore Scalability and Sustained Throughput across Test Systems
# ==============================================================================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.2, 2.7), constrained_layout=True)

test_systems = ['IEEE 14', 'IEEE 57', 'IEEE 118', 'IEEE 300', 'PEGASE 1354']
buses = [14, 57, 118, 300, 1354]
time_1core = [0.52, 5.19, 21.40, 102.30, 2323.64] # ms
time_10core = [0.34, 4.87, 5.21, 14.21, 295.35]   # ms
speedups = [t1 / t10 for t1, t10 in zip(time_1core, time_10core)]
throughput_n1 = [58715, 16442, 35687, 28927, 6741] # solve/s
throughput_n2 = [40844, 80241, 70867, 34267, 7817] # solve/s

# (a) Speedup with 10-core C++20 Lock-Free Concurrency
x = np.arange(len(test_systems))
width = 0.35

ax1.plot(x, speedups, marker='s', color='#0055aa', linewidth=2, markersize=6, label='Measured Speedup (10-Core)')
ax1.axhline(10.0, color='gray', linestyle=':', label='Theoretical Linear (10x)')
ax1.set_xticks(x)
ax1.set_xticklabels(test_systems)
ax1.set_ylabel(r'Parallel Speedup ($\times$)')
ax1.set_xlabel('Power System Test Case')
ax1.set_title('(a) Multicore Scaling (Lock-Free Work Stealing)', fontweight='bold')
ax1.set_ylim(0, 11)
ax1.grid(True)
ax1.legend(loc='upper left', framealpha=0.9)

for i, sp in enumerate(speedups):
    ax1.annotate(f'{sp:.2f}x', (x[i], sp + 0.35), ha='center', fontsize=8, fontweight='bold')

# (b) Sustained AC Solves per Second
ax2.bar(x - width/2, throughput_n1, width, label='N-1 Screening Rate', color='#3377bb', edgecolor='black')
ax2.bar(x + width/2, throughput_n2, width, label='N-2 Screening Rate', color='#003366', edgecolor='black')
ax2.set_xticks(x)
ax2.set_xticklabels(test_systems)
ax2.set_ylabel('Throughput (Full AC Solves / sec)')
ax2.set_xlabel('Power System Test Case')
ax2.set_title('(b) Sustained Simulation Throughput', fontweight='bold')
ax2.grid(True, axis='y')
ax2.legend(loc='upper right', framealpha=0.9)

for i in range(len(test_systems)):
    ax2.text(x[i] + width/2, throughput_n2[i] + 1800, f'{throughput_n2[i]:,}', ha='center', fontsize=7, rotation=0)

plt.savefig('assets/fig_multicore_scaling.png', dpi=600)
plt.close()
print("Generated assets/fig_multicore_scaling.png")

# ==============================================================================
# Figure 3: Complete 1.98M N-2 Ground Truth Distribution & Vulnerabilities
# ==============================================================================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7.2, 2.7), constrained_layout=True)

# (a) Pie Chart of N-2 States
categories = ['Converged & Stable\n(1,018,275 cases)', 
              'Topological Islanding\n(959,883 cases)', 
              'Voltage Collapse\n(2,887 cases)']
shares = [51.40, 48.45, 0.15]
pie_colors = ['#2ca02c', '#d62728', '#1f77b4']
explode = (0, 0.05, 0.15)

wedges, texts, autotexts = ax1.pie(
    shares, labels=categories, autopct='%1.2f%%', startangle=140,
    colors=pie_colors, explode=explode, textprops={'fontsize': 7.5},
    wedgeprops={'edgecolor': 'black', 'linewidth': 0.8}
)
for at in autotexts:
    at.set_fontsize(7.5)
    at.set_fontweight('bold')
ax1.set_title('(a) Classification of 1,981,045 N-2 Outages\n(PEGASE 1354)', fontweight='bold')

# (b) Top Critical Double Outages: Unserved Load vs Single N-1
top_pairs = ['#208 & #209', '#206 & #207', '#76 & #434', '#107 & #108', '#76 & #78']
n2_unserved = [1832.0, 1770.0, 1431.0, 1048.0, 980.5]
n1_unserved = [0.0, 0.0, 0.0, 0.0, 0.0] # All zero under single outage!

y = np.arange(len(top_pairs))
ax2.barh(y - 0.18, n2_unserved, height=0.35, label='N-2 Double Circuit Outage', color='#d62728', edgecolor='black')
ax2.barh(y + 0.18, n1_unserved, height=0.35, label='N-1 Single Circuit Outage (0 MW)', color='#2ca02c', edgecolor='black')
ax2.set_yticks(y)
ax2.set_yticklabels(top_pairs)
ax2.set_xlabel('Unserved Demand (MW)')
ax2.set_title('(b) Parallel Circuit Common-Corridor Blackout Risk', fontweight='bold')
ax2.grid(True, axis='x')
ax2.legend(loc='lower right', framealpha=0.9, fontsize=7.5)

for i, mw in enumerate(n2_unserved):
    ax2.text(mw + 25, y[i] - 0.18, f'{mw:.1f} MW', va='center', fontsize=7.5, fontweight='bold')
ax2.set_xlim(0, 2200)

plt.savefig('assets/fig_n2_distribution.png', dpi=600)
plt.close()
print("Generated assets/fig_n2_distribution.png")
