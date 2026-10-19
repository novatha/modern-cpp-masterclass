"""
Visualisasi Topologi Jaringan Tenaga Listrik Bebas Overlap
Fitur:
1. Algoritma Collision-Relaxation & Clearance Distance (Mencegah Bus & Label Tumpang Tindih)
2. Panjang edge (jarak geometris) berbanding lurus dengan impedansi saluran Z = sqrt(R^2 + X^2)
3. Ekspor format Statis Bebas-Tabrakan (High-Res PNG) dan Format Interaktif Dinamis (HTML/Vis.js Physics)
"""

import re
import json
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from pathlib import Path


def parse_matpower(fpath):
    """
    Parse berkas MATPOWER .m untuk mengekstrak bus, generator, dan saluran transmisi.
    """
    with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # 1. Parse bus
    bus_match = re.search(r'mpc\.bus\s*=\s*\[(.*?)\];', content, re.DOTALL)
    buses = {}
    if bus_match:
        for line in bus_match.group(1).strip().split('\n'):
            line = line.split('%')[0].strip()
            if not line:
                continue
            parts = line.replace(';', '').split()
            if len(parts) >= 4:
                b_id = int(parts[0])
                b_type = int(parts[1])  # 1=PQ, 2=PV, 3=Slack
                pd = float(parts[2])
                qd = float(parts[3])
                vm = float(parts[7]) if len(parts) > 7 else 1.0
                va = float(parts[8]) if len(parts) > 8 else 0.0
                buses[b_id] = {
                    'type': b_type,
                    'Pd': pd,
                    'Qd': qd,
                    'Vm': vm,
                    'Va': va,
                    'Pg': 0.0,
                    'Qg': 0.0
                }

    # 2. Parse generator
    gen_match = re.search(r'mpc\.gen\s*=\s*\[(.*?)\];', content, re.DOTALL)
    if gen_match:
        for line in gen_match.group(1).strip().split('\n'):
            line = line.split('%')[0].strip()
            if not line:
                continue
            parts = line.replace(';', '').split()
            if len(parts) >= 4:
                gbus = int(parts[0])
                pg = float(parts[1])
                qg = float(parts[2])
                stat = int(parts[7]) if len(parts) > 7 else 1
                if stat > 0 and gbus in buses:
                    buses[gbus]['Pg'] += pg
                    buses[gbus]['Qg'] += qg

    # 3. Parse branch (saluran)
    branch_match = re.search(r'mpc\.branch\s*=\s*\[(.*?)\];', content, re.DOTALL)
    branches = []
    if branch_match:
        for idx, line in enumerate(branch_match.group(1).strip().split('\n')):
            line = line.split('%')[0].strip()
            if not line:
                continue
            parts = line.replace(';', '').split()
            if len(parts) >= 5:
                fbus = int(parts[0])
                tbus = int(parts[1])
                r = float(parts[2])
                x = float(parts[3])
                status = int(parts[10]) if len(parts) > 10 else 1
                rateA = float(parts[5]) if len(parts) > 5 else 0.0
                if status != 0:
                    z = float(np.sqrt(r**2 + x**2))
                    branches.append((fbus, tbus, r, x, z, rateA, idx + 1))

    return buses, branches


def compute_collision_free_layout(G, num_buses):
    """
    Menghitung posisi koordinat bus dengan garansi bebas tumpang tindih (Clearance floor)
    dan proporsionalitas terhadap impedansi saluran Z_ij.
    """
    z_vals = [d['weight'] for _, _, d in G.edges(data=True)]
    mean_z = float(np.mean(z_vals)) if z_vals else 0.1

    # Adaptasi parameter berdasarkan ukuran sistem
    if num_buses <= 20:
        base_clearance = 0.50
        z_factor = 2.0
        scale = 3.5
        min_clearance = 0.55
        max_iters = 300
    elif num_buses <= 60:
        base_clearance = 0.45
        z_factor = 2.5
        scale = 6.0
        min_clearance = 0.48
        max_iters = 400
    elif num_buses <= 150:
        base_clearance = 0.35
        z_factor = 2.8
        scale = 9.0
        min_clearance = 0.38
        max_iters = 500
    else:
        base_clearance = 0.25
        z_factor = 3.0
        scale = 14.0
        min_clearance = 0.28
        max_iters = 600

    # 1. Target distance dengan clearance floor agar saluran impedansi sangat kecil
    # tidak membuat bus bertumpukan
    for u, v, d in G.edges(data=True):
        z = d['weight']
        d['target_dist'] = base_clearance + z_factor * (z / max(mean_z, 1e-4))

    # 2. Kamada-Kawai Energy Minimization awal
    dist_matrix = dict(nx.all_pairs_dijkstra_path_length(G, weight='target_dist'))
    pos = nx.kamada_kawai_layout(G, dist=dist_matrix, scale=scale)

    # 3. Iteratif Hard-Sphere Collision-Relaxation Solver (Vektorisasi NumPy)
    nodes = list(pos.keys())
    coords = np.array([pos[k] for k in nodes], dtype=float)
    N = len(nodes)

    for _ in range(max_iters):
        # Selisih pairwise (N, N, 2)
        diff = coords[:, np.newaxis, :] - coords[np.newaxis, :, :]
        dist = np.linalg.norm(diff, axis=-1)
        np.fill_diagonal(dist, np.inf)

        # Cari pasangan yang terlalu dekat
        overlap_mask = dist < min_clearance
        if not np.any(overlap_mask):
            break

        overlap = np.clip((min_clearance - dist) * 0.5, 0.0, None)
        dist_safe = np.where(dist < 1e-5, 1e-5, dist)
        force_dir = diff / dist_safe[:, :, np.newaxis]
        displacement = np.sum(force_dir * overlap[:, :, np.newaxis], axis=1)
        coords += displacement * 0.6

    return {nodes[i]: coords[i] for i in range(N)}


def plot_topology_impedance(case_path, out_img):
    """
    Merender gambar topologi statis resolusi tinggi (PNG) bebas tabrakan node.
    """
    buses, branches = parse_matpower(case_path)
    if not buses:
        raise ValueError(f"Tidak ada data bus valid pada: {case_path}")

    G = nx.Graph()
    for b_id, b_data in buses.items():
        G.add_node(b_id, **b_data)

    for fbus, tbus, r, x, z, rateA, b_idx in branches:
        G.add_edge(fbus, tbus, r=r, x=x, weight=z, rateA=rateA, idx=b_idx)

    num_buses = len(buses)
    num_branches = len(branches)

    # Adaptasi parameter tampilan visual
    if num_buses <= 20:
        fig_w, fig_h = 16, 12
        node_size = 750
        bus_font_size = 10.5
        show_edge_labels = True
        show_mw_labels = True
        line_w_base = 2.8
    elif num_buses <= 60:
        fig_w, fig_h = 22, 16
        node_size = 460
        bus_font_size = 8.5
        show_edge_labels = False  # Matikan label teks saluran agar visual bersih
        show_mw_labels = True
        line_w_base = 2.0
    elif num_buses <= 150:
        fig_w, fig_h = 28, 20
        node_size = 240
        bus_font_size = 7.0
        show_edge_labels = False
        show_mw_labels = False
        line_w_base = 1.4
    else:
        fig_w, fig_h = 36, 26
        node_size = 120
        bus_font_size = 5.5
        show_edge_labels = False
        show_mw_labels = False
        line_w_base = 1.0

    pos = compute_collision_free_layout(G, num_buses)

    fig, ax = plt.subplots(figsize=(fig_w, fig_h), dpi=250)
    fig.patch.set_facecolor('#ffffff')
    ax.set_facecolor('#ffffff')

    z_values = [G[u][v]['weight'] for u, v in G.edges()]
    z_min, z_max = min(z_values), max(z_values)
    cmap = plt.colormaps['plasma']

    # 1. Gambar Saluran Transmisi
    for u, v in G.edges():
        z = G[u][v]['weight']
        p1, p2 = pos[u], pos[v]
        norm_z = (z - z_min) / (z_max - z_min + 1e-6)
        color = cmap(norm_z)
        width = max(1.0, line_w_base * (1.2 - 0.4 * norm_z))
        ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=color, linewidth=width, zorder=2, alpha=0.85)

        if show_edge_labels:
            mid_x = (p1[0] + p2[0]) / 2.0
            mid_y = (p1[1] + p2[1]) / 2.0
            dx, dy = p2[0] - p1[0], p2[1] - p1[1]
            length = np.hypot(dx, dy)
            offset_x = -dy / length * 0.08 if length > 0 else 0
            offset_y = dx / length * 0.08 if length > 0 else 0
            ax.text(mid_x + offset_x, mid_y + offset_y, f"{z:.3f}",
                    fontsize=max(6.5, bus_font_size - 2), color='#334155', fontweight='bold',
                    ha='center', va='center',
                    bbox=dict(boxstyle='round,pad=0.18', facecolor='#f8fafc', edgecolor='#cbd5e1', alpha=0.85),
                    zorder=4)

    # 2. Gambar Node Bus
    for b_id in G.nodes():
        p = pos[b_id]
        b_type = G.nodes[b_id]['type']
        if b_type == 3:  # Slack
            color, edge_c, s_mult = '#dc2626', '#7f1d1d', 1.3
        elif b_type == 2:  # PV
            color, edge_c, s_mult = '#ea580c', '#9a3412', 1.1
        else:  # PQ
            color, edge_c, s_mult = '#2563eb', '#1e3a8a', 1.0

        ax.scatter(p[0], p[1], s=node_size * s_mult, c=color,
                   edgecolors=edge_c, linewidths=1.8, zorder=5)

        ax.text(p[0], p[1], str(b_id), fontsize=bus_font_size, color='white',
                fontweight='bold', ha='center', va='center', zorder=6)

        if show_mw_labels:
            pd = G.nodes[b_id]['Pd']
            pg = G.nodes[b_id]['Pg']
            lbl_txt = ""
            if pg > 0 and pd > 0:
                lbl_txt = f"+{pg:.0f} / -{pd:.0f}M"
            elif pg > 0:
                lbl_txt = f"+{pg:.0f}MW"
            elif pd > 0:
                lbl_txt = f"{pd:.1f}MW"

            if lbl_txt:
                offset_y = -0.16 if num_buses <= 20 else -0.22
                ax.text(p[0], p[1] + offset_y, lbl_txt,
                        fontsize=bus_font_size - 2.0, color='#1e293b',
                        fontweight='semibold', ha='center', va='top', zorder=6,
                        bbox=dict(boxstyle='round,pad=0.12', facecolor='#ffffff', edgecolor='#cbd5e1', alpha=0.9))

    # Colorbar
    sm = plt.cm.ScalarMappable(cmap=cmap, norm=plt.Normalize(vmin=z_min, vmax=z_max))
    sm.set_array([])
    cbar = plt.colorbar(sm, ax=ax, fraction=0.025, pad=0.02, aspect=30)
    cbar.set_label('Impedansi Saluran $Z_{ij} = \\sqrt{R^2 + X^2}$ (p.u.)\n(Panjang Geometris Edge Berbanding Lurus dengan $Z_{ij}$)',
                   fontsize=10, fontweight='bold', labelpad=10)
    cbar.ax.tick_params(labelsize=8.5)

    case_name = Path(case_path).name
    plt.title(f'Topologi Jaringan Transmisi {case_name} ({num_buses} Bus, {num_branches} Saluran)\n'
              f'Layout Bebas Tabrakan (Collision-Relaxation) & Proporsional Impedansi Kompleks ($d_{{ij}} \\propto Z_{{ij}}$)',
              fontsize=13, fontweight='bold', pad=15, color='#0f172a')

    legend_elements = [
        Line2D([0], [0], marker='o', color='w', label='Bus Slack / Generator Utama',
               markerfacecolor='#dc2626', markeredgecolor='#7f1d1d', markersize=11, markeredgewidth=1.5),
        Line2D([0], [0], marker='o', color='w', label='Bus Generator PV',
               markerfacecolor='#ea580c', markeredgecolor='#9a3412', markersize=10, markeredgewidth=1.5),
        Line2D([0], [0], marker='o', color='w', label='Bus Beban PQ',
               markerfacecolor='#2563eb', markeredgecolor='#1e3a8a', markersize=9, markeredgewidth=1.5),
        Line2D([0], [0], color='#475569', linewidth=2.5, label='Saluran Transmisi ($d \\propto Z$)')
    ]
    ax.legend(handles=legend_elements, loc='upper left', fontsize=9, framealpha=0.95, edgecolor='#cbd5e1')

    ax.axis('off')
    plt.tight_layout()

    out_path = Path(out_img)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out_path, dpi=250, bbox_inches='tight')
    plt.close()
    print(f"✓ Gambar topologi bersih disimpan di: {out_path}")


def export_interactive_html(case_path, out_html):
    """
    Mengekspor visualisasi topologi jaringan interaktif berbasis web (Vis.js).
    Mendukung fisika anti-gravity, drag & drop bus, zoom/pan vektor tanpa batas,
    dan tooltip parameter daya real-time.
    """
    buses, branches = parse_matpower(case_path)
    if not buses:
        raise ValueError(f"Tidak ada data bus valid pada: {case_path}")

    nodes_data = []
    for b_id, b_data in buses.items():
        b_type = b_data['type']
        if b_type == 3:
            color = "#ef4444"
            type_name = "Bus Slack (Generator Utama)"
            size = 24
        elif b_type == 2:
            color = "#f97316"
            type_name = "Bus Generator PV"
            size = 20
        else:
            color = "#3b82f6"
            type_name = "Bus Beban PQ"
            size = 16

        tooltip = (
            f"<div style='font-family: Segoe UI, sans-serif; font-size: 13px; line-height: 1.5; color: #f8fafc;'>"
            f"<b style='font-size: 14px; color: #38bdf8;'>⚡ Gardu Induk / Bus #{b_id}</b><br>"
            f"<b>Klasifikasi:</b> {type_name}<br>"
            f"<b>Tegangan V:</b> {b_data['Vm']:.3f} p.u. (Sudut: {b_data['Va']:.1f}°)<br>"
            f"<b>Beban Pd:</b> {b_data['Pd']:.1f} MW | <b>Qd:</b> {b_data['Qd']:.1f} MVAr<br>"
        )
        if b_data['Pg'] > 0:
            tooltip += f"<b style='color: #4ade80;'>Pembangkit Pg:</b> {b_data['Pg']:.1f} MW | <b>Qg:</b> {b_data['Qg']:.1f} MVAr<br>"
        tooltip += "</div>"

        nodes_data.append({
            "id": b_id,
            "label": f"Bus {b_id}",
            "color": {
                "background": color,
                "border": "#ffffff",
                "highlight": {"background": "#38bdf8", "border": "#ffffff"},
                "hover": {"background": "#60a5fa", "border": "#ffffff"}
            },
            "title": tooltip,
            "font": {"color": "#ffffff", "face": "Segoe UI, sans-serif", "size": 13, "bold": True},
            "size": size,
            "shadow": {"enabled": True, "color": "rgba(0,0,0,0.5)", "size": 6, "x": 2, "y": 2}
        })

    edges_data = []
    z_vals = [b[4] for b in branches]
    z_min, z_max = min(z_vals), max(z_vals)
    mean_z = float(np.mean(z_vals)) if z_vals else 0.1

    num_b = len(buses)
    base_spring = 100 if num_b <= 30 else (140 if num_b <= 100 else 180)

    for fbus, tbus, r, x, z, rateA, b_idx in branches:
        norm_z = (z - z_min) / (z_max - z_min + 1e-6)
        target_len = int(base_spring + norm_z * 220)

        tooltip = (
            f"<div style='font-family: Segoe UI, sans-serif; font-size: 12px; line-height: 1.4; color: #f8fafc;'>"
            f"<b style='color: #38bdf8;'>Transmisi #{b_idx}: Bus {fbus} ➔ Bus {tbus}</b><br>"
            f"<b>Impedansi Z:</b> {z:.5f} p.u.<br>"
            f"<b>Resistansi R:</b> {r:.5f} p.u.<br>"
            f"<b>Reaktansi X:</b> {x:.5f} p.u.<br>"
        )
        if rateA > 0:
            tooltip += f"<b>Kapasitas Termal RateA:</b> {rateA:.1f} MVA<br>"
        tooltip += "</div>"

        edges_data.append({
            "from": fbus,
            "to": tbus,
            "length": target_len,
            "title": tooltip,
            "color": {"color": "#64748b", "highlight": "#38bdf8", "hover": "#38bdf8"},
            "width": 2,
            "smooth": {"type": "continuous", "roundness": 0.1}
        })

    # Cek apakah berkas vis-network lokal tersedia
    out_file = Path(out_html)
    assets_dir = out_file.parent
    local_vis = assets_dir / "vis-network.min.js"
    if not local_vis.exists():
        import sys
        import shutil
        candidates = [
            Path(__file__).resolve().parent.parent / "assets" / "vis-network.min.js",
            Path(getattr(sys, '_MEIPASS', '')) / "assets" / "vis-network.min.js" if getattr(sys, 'frozen', False) else None
        ]
        for cand in candidates:
            if cand and cand.exists():
                try:
                    shutil.copy2(cand, local_vis)
                    break
                except Exception:
                    pass

    js_src = "vis-network.min.js" if local_vis.exists() else "https://unpkg.com/vis-network@9.1.9/standalone/umd/vis-network.min.js"

    case_name = Path(case_path).name

    html_template = f"""<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="utf-8">
  <title>GridGuard - Topologi Interaktif {case_name}</title>
  <script type="text/javascript" src="{js_src}"></script>
  <style>
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      padding: 0;
      background-color: #030712;
      font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, 'Helvetica Neue', sans-serif;
      color: #f8fafc;
      overflow: hidden;
      width: 100vw;
      height: 100vh;
    }}
    #network {{
      width: 100%;
      height: 100%;
      position: absolute;
      top: 0;
      left: 0;
    }}
    #toolbar {{
      position: absolute;
      top: 16px;
      left: 16px;
      z-index: 1000;
      background: rgba(17, 24, 39, 0.92);
      backdrop-filter: blur(8px);
      padding: 14px 20px;
      border-radius: 10px;
      border: 1px solid #334155;
      box-shadow: 0 10px 25px rgba(0,0,0,0.6);
      max-width: 480px;
    }}
    h1 {{
      margin: 0 0 6px 0;
      color: #38bdf8;
      font-size: 16px;
      font-weight: 700;
      letter-spacing: 0.3px;
    }}
    p {{
      margin: 0 0 10px 0;
      font-size: 12px;
      color: #94a3b8;
      line-height: 1.4;
    }}
    .badges {{
      display: flex;
      gap: 6px;
      flex-wrap: wrap;
      margin-bottom: 12px;
    }}
    .badge {{
      display: inline-flex;
      align-items: center;
      gap: 5px;
      padding: 4px 8px;
      border-radius: 5px;
      font-size: 11px;
      font-weight: 600;
    }}
    .badge-slack {{ background: #991b1b; color: #fecaca; border: 1px solid #ef4444; }}
    .badge-pv {{ background: #9a3412; color: #fed7aa; border: 1px solid #f97316; }}
    .badge-pq {{ background: #1e3a8a; color: #bfdbfe; border: 1px solid #3b82f6; }}
    .controls {{
      display: flex;
      gap: 6px;
    }}
    button {{
      background: #1e293b;
      color: #f8fafc;
      border: 1px solid #475569;
      border-radius: 6px;
      padding: 6px 12px;
      font-size: 11px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.15s ease;
    }}
    button:hover {{
      background: #334155;
      border-color: #38bdf8;
      color: #38bdf8;
    }}
    #status-hint {{
      position: absolute;
      bottom: 16px;
      right: 16px;
      background: rgba(17, 24, 39, 0.85);
      border: 1px solid #334155;
      padding: 8px 14px;
      border-radius: 8px;
      font-size: 11px;
      color: #cbd5e1;
      pointer-events: none;
    }}
  </style>
</head>
<body>
  <div id="toolbar">
    <h1>⚡ Topologi Transmisi Interaktif: {case_name}</h1>
    <p>💡 <b>Bebas Tumpang Tindih:</b> Geser (drag) sembarang bus untuk merapikan tata letak • Scroll mouse / pinch untuk zoom • Arahkan kursor untuk info daya & impedansi $Z$.</p>
    <div class="badges">
      <span class="badge badge-slack">● Bus Slack (Gen Utama)</span>
      <span class="badge badge-pv">● Bus PV (Pembangkit)</span>
      <span class="badge badge-pq">● Bus PQ (Beban)</span>
    </div>
    <div class="controls">
      <button onclick="network.fit({{animation: true}})">⛶ Fit Layar</button>
      <button onclick="togglePhysics()">⏸ Toggle Fisika</button>
      <button onclick="stabilizeAndStop()">🔒 Kunci Posisi</button>
    </div>
  </div>

  <div id="status-hint">💡 Klik & tahan node untuk memindahkan • Tarik latar belakang untuk pan</div>
  <div id="network"></div>

  <script>
    var nodes = new vis.DataSet({json.dumps(nodes_data)});
    var edges = new vis.DataSet({json.dumps(edges_data)});
    var container = document.getElementById('network');
    var data = {{ nodes: nodes, edges: edges }};
    
    var physicsEnabled = true;
    var options = {{
      physics: {{
        enabled: true,
        barnesHut: {{
          gravitationalConstant: -4200,
          centralGravity: 0.22,
          springLength: {base_spring},
          springConstant: 0.045,
          damping: 0.12,
          avoidOverlap: 1.0
        }},
        stabilization: {{
          enabled: true,
          iterations: 350,
          updateInterval: 25
        }}
      }},
      interaction: {{
        hover: true,
        hoverConnectedEdges: true,
        tooltipDelay: 100,
        zoomView: true,
        dragView: true,
        dragNodes: true
      }},
      nodes: {{
        borderWidth: 2,
        borderWidthSelected: 3
      }}
    }};

    var network = new vis.Network(container, data, options);

    function togglePhysics() {{
      physicsEnabled = !physicsEnabled;
      network.setOptions({{ physics: {{ enabled: physicsEnabled }} }});
    }}

    function stabilizeAndStop() {{
      network.setOptions({{ physics: {{ enabled: false }} }});
      physicsEnabled = false;
    }}
  </script>
</body>
</html>
"""

    out_file.parent.mkdir(parents=True, exist_ok=True)
    with open(out_file, "w", encoding="utf-8") as f:
        f.write(html_template)
    print(f"✓ Berkas topologi interaktif disimpan di: {out_file}")


if __name__ == "__main__":
    case_test = "data/case14.m"
    plot_topology_impedance(case_test, "assets/topology_ieee14_impedance.png")
    export_interactive_html(case_test, "assets/topology_case14_interactive.html")
