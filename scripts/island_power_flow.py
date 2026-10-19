"""
Modul Studi Aliran Daya Multi-Island (Island Power Flow Study Engine)
Untuk Analisis Kontingensi Sistem Tenaga Listrik (PLN / Grid Operator)

Fungsi Utama:
1. Dekomposisi graf jaringan menjadi sub-jaringan independen (islanding).
2. Klasifikasi tipe island:
   - Energized (Aktif Seimbang): Memiliki pembangkit memadai, penunjukan Slack Bus lokal, solusi AC Newton-Raphson.
   - Defisit Daya (UFLS): Kapasitas pembangkit < beban, perhitungan pelepasan beban darurat (Under-Frequency Load Shedding).
   - Padam Total (Blackout / De-energized): Tidak ada pembangkit, beban padam 100%.
3. Eksekusi AC Power Flow (Newton-Raphson) per-island.
4. Pelaporan detail profil tegangan, aliran daya saluran, rugi transmisi, dan neraca daya tiap island.
"""

import argparse
import numpy as np
import networkx as nx
from pathlib import Path


def parse_matpower(fpath):
    """
    Ekstraksi data bus, generator, dan branch dari berkas MATPOWER .m.
    """
    import re
    with open(fpath, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    baseMVA = 100.0
    m_base = re.search(r'mpc\.baseMVA\s*=\s*([0-9\.]+);', content)
    if m_base:
        baseMVA = float(m_base.group(1))

    # Parse bus
    bus_match = re.search(r'mpc\.bus\s*=\s*\[(.*?)\];', content, re.DOTALL)
    buses = {}
    if bus_match:
        for line in bus_match.group(1).strip().split('\n'):
            line = line.split('%')[0].strip()
            if not line: continue
            parts = line.replace(';', '').split()
            if len(parts) >= 4:
                b_id = int(parts[0])
                b_type = int(parts[1])  # 1=PQ, 2=PV, 3=Slack
                pd = float(parts[2])
                qd = float(parts[3])
                vm = float(parts[7]) if len(parts) > 7 else 1.0
                va = float(parts[8]) if len(parts) > 8 else 0.0
                buses[b_id] = {
                    'type': b_type, 'Pd': pd, 'Qd': qd,
                    'Vm': vm, 'Va': va, 'Pg': 0.0, 'Qg': 0.0,
                    'Pmax': 0.0, 'Pmin': 0.0, 'Qmax': 0.0, 'Qmin': 0.0,
                    'has_gen': False
                }

    # Parse gen
    gen_match = re.search(r'mpc\.gen\s*=\s*\[(.*?)\];', content, re.DOTALL)
    if gen_match:
        for line in gen_match.group(1).strip().split('\n'):
            line = line.split('%')[0].strip()
            if not line: continue
            parts = line.replace(';', '').split()
            if len(parts) >= 5:
                gbus = int(parts[0])
                pg = float(parts[1])
                qg = float(parts[2])
                qmax = float(parts[3]) if len(parts) > 3 else 999.0
                qmin = float(parts[4]) if len(parts) > 4 else -999.0
                stat = int(parts[7]) if len(parts) > 7 else 1
                pmax = float(parts[8]) if len(parts) > 8 else pg * 1.5
                pmin = float(parts[9]) if len(parts) > 9 else 0.0
                if stat > 0 and gbus in buses:
                    buses[gbus]['Pg'] += pg
                    buses[gbus]['Qg'] += qg
                    buses[gbus]['Pmax'] += max(pmax, pg)
                    buses[gbus]['Pmin'] += pmin
                    buses[gbus]['Qmax'] += qmax
                    buses[gbus]['Qmin'] += qmin
                    if pg > 0 or pmax > 0:
                        buses[gbus]['has_gen'] = True

    # Parse branch
    branch_match = re.search(r'mpc\.branch\s*=\s*\[(.*?)\];', content, re.DOTALL)
    branches = []
    if branch_match:
        for idx, line in enumerate(branch_match.group(1).strip().split('\n')):
            line = line.split('%')[0].strip()
            if not line: continue
            parts = line.replace(';', '').split()
            if len(parts) >= 5:
                fbus = int(parts[0])
                tbus = int(parts[1])
                r = float(parts[2])
                x = float(parts[3])
                rateA = float(parts[5]) if len(parts) > 5 else 0.0
                status = int(parts[10]) if len(parts) > 10 else 1
                if status != 0:
                    z = float(np.sqrt(r**2 + x**2))
                    branches.append((fbus, tbus, r, x, z, rateA, idx + 1))

    return buses, branches, baseMVA


def solve_ac_power_flow_island(buses, sub_branches, isl_nodes, baseMVA=100.0, max_iter=30, tol=1e-4):
    """
    Menyelesaikan aliran daya AC Newton-Raphson untuk satu pulau (island) kelistrikan.
    """
    N = len(isl_nodes)
    if N == 0:
        return {"status": "EMPTY", "buses": {}, "branches": [], "Ploss": 0.0}

    # Jika hanya 1 bus
    if N == 1:
        b = isl_nodes[0]
        has_gen = buses[b]['has_gen']
        pd = buses[b]['Pd']
        if not has_gen or buses[b]['Pmax'] < pd:
            return {
                "status": "BLACKOUT",
                "reason": "Bus tunggal tanpa pembangkit yang memadai.",
                "slack_bus": None,
                "buses": {b: {"Vm": 0.0, "Va_deg": 0.0, "Pd": pd, "Qd": buses[b]['Qd'], "Pg": 0.0, "Qg": 0.0}},
                "branches": [],
                "Ploss": 0.0
            }
        return {
            "status": "CONVERGED",
            "iterations": 0,
            "slack_bus": b,
            "buses": {b: {"Vm": buses[b]['Vm'], "Va_deg": 0.0, "Pd": pd, "Qd": buses[b]['Qd'], "Pg": pd, "Qg": buses[b]['Qd']}},
            "branches": [],
            "Ploss": 0.0
        }

    node_to_idx = {b: i for i, b in enumerate(isl_nodes)}

    # Identifikasi bus pembangkit dalam island
    gen_nodes = [b for b in isl_nodes if buses[b]['has_gen']]
    total_load_p = sum(buses[b]['Pd'] for b in isl_nodes)
    total_cap_p = sum(buses[b]['Pmax'] for b in gen_nodes)

    if not gen_nodes or total_cap_p <= 0.01:
        # Tidak ada generator sama sekali -> Padam Total
        return {
            "status": "BLACKOUT",
            "reason": "Tidak ada unit pembangkit yang beroperasi di dalam island.",
            "slack_bus": None,
            "buses": {b: {"Vm": 0.0, "Va_deg": 0.0, "Pd": buses[b]['Pd'], "Qd": buses[b]['Qd'], "Pg": 0.0, "Qg": 0.0} for b in isl_nodes},
            "branches": [],
            "Ploss": 0.0
        }

    # Cek defisit daya & UFLS
    ufls_shed_mw = 0.0
    is_deficit = False
    scale_load = 1.0
    if total_cap_p < total_load_p:
        is_deficit = True
        ufls_shed_mw = total_load_p - total_cap_p * 0.95  # Sisakan cadangan margin 5%
        scale_load = max(0.0, (total_load_p - ufls_shed_mw) / max(total_load_p, 1e-4))

    # Tentukan Local Slack Bus
    slack_node = None
    for b in isl_nodes:
        if buses[b]['type'] == 3:  # Slack asli
            slack_node = b
            break
    if slack_node is None:
        # Pilih generator dengan kapasitas Pmax tertinggi di dalam island
        slack_node = max(gen_nodes, key=lambda b: (buses[b]['Pmax'], buses[b]['Pg']))

    slack_idx = node_to_idx[slack_node]

    # Klasifikasi tipe bus lokal: 1=PQ, 2=PV, 3=Slack
    bus_type = []
    for b in isl_nodes:
        if b == slack_node:
            bus_type.append(3)
        elif b in gen_nodes:
            bus_type.append(2)
        else:
            bus_type.append(1)

    # Matriks Admitansi Ybus
    Ybus = np.zeros((N, N), dtype=complex)
    for f, t, r, x, z, rateA, idx in sub_branches:
        i, j = node_to_idx[f], node_to_idx[t]
        y_se = 1.0 / complex(r, x)
        Ybus[i, i] += y_se
        Ybus[j, j] += y_se
        Ybus[i, j] -= y_se
        Ybus[j, i] -= y_se

    # Inisialisasi tegangan
    Vm = np.ones(N)
    Va = np.zeros(N)
    for i, b in enumerate(isl_nodes):
        if bus_type[i] in [2, 3]:
            Vm[i] = buses[b]['Vm'] if buses[b]['Vm'] > 0 else 1.0

    # Daya terjadwal p.u.
    P_spec = np.zeros(N)
    Q_spec = np.zeros(N)
    for i, b in enumerate(isl_nodes):
        pd_eff = buses[b]['Pd'] * scale_load
        qd_eff = buses[b]['Qd'] * scale_load
        P_spec[i] = (buses[b]['Pg'] - pd_eff) / baseMVA
        Q_spec[i] = (buses[b]['Qg'] - qd_eff) / baseMVA

    pv_pq = [i for i in range(N) if bus_type[i] in [1, 2]]
    pq = [i for i in range(N) if bus_type[i] == 1]

    G = Ybus.real
    B = Ybus.imag
    converged = False
    actual_iters = 0

    for it in range(max_iter):
        actual_iters = it + 1
        P_calc = np.zeros(N)
        Q_calc = np.zeros(N)

        for i in range(N):
            for j in range(N):
                th_ij = Va[i] - Va[j]
                P_calc[i] += Vm[i] * Vm[j] * (G[i, j] * np.cos(th_ij) + B[i, j] * np.sin(th_ij))
                Q_calc[i] += Vm[i] * Vm[j] * (G[i, j] * np.sin(th_ij) - B[i, j] * np.cos(th_ij))

        # Mismatch
        d_P = P_spec[pv_pq] - P_calc[pv_pq]
        d_Q = Q_spec[pq] - Q_calc[pq] if pq else np.array([])
        mismatch = np.concatenate([d_P, d_Q]) if pq else d_P

        if np.max(np.abs(mismatch)) < tol:
            converged = True
            break

        # Jacobian
        n_pv_pq = len(pv_pq)
        n_pq = len(pq)
        J11 = np.zeros((n_pv_pq, n_pv_pq))
        J12 = np.zeros((n_pv_pq, n_pq))
        J21 = np.zeros((n_pq, n_pv_pq))
        J22 = np.zeros((n_pq, n_pq))

        for row, i in enumerate(pv_pq):
            for col, j in enumerate(pv_pq):
                if i == j:
                    J11[row, col] = -Q_calc[i] - (Vm[i]**2) * B[i, i]
                else:
                    th_ij = Va[i] - Va[j]
                    J11[row, col] = Vm[i] * Vm[j] * (G[i, j] * np.sin(th_ij) - B[i, j] * np.cos(th_ij))

            for col, j in enumerate(pq):
                if i == j:
                    J12[row, col] = P_calc[i] / Vm[i] + Vm[i] * G[i, i]
                else:
                    th_ij = Va[i] - Va[j]
                    J12[row, col] = Vm[i] * (G[i, j] * np.cos(th_ij) + B[i, j] * np.sin(th_ij))

        for row, i in enumerate(pq):
            for col, j in enumerate(pv_pq):
                if i == j:
                    J21[row, col] = P_calc[i] - (Vm[i]**2) * G[i, i]
                else:
                    th_ij = Va[i] - Va[j]
                    J21[row, col] = -Vm[i] * Vm[j] * (G[i, j] * np.cos(th_ij) + B[i, j] * np.sin(th_ij))

            for col, j in enumerate(pq):
                if i == j:
                    J22[row, col] = Q_calc[i] / Vm[i] - Vm[i] * B[i, i]
                else:
                    th_ij = Va[i] - Va[j]
                    J22[row, col] = Vm[i] * (G[i, j] * np.sin(th_ij) - B[i, j] * np.cos(th_ij))

        if n_pq > 0:
            J = np.block([[J11, J12], [J21, J22]])
        else:
            J = J11

        try:
            dx = np.linalg.solve(J, mismatch)
            Va[pv_pq] += dx[:n_pv_pq]
            if n_pq > 0:
                Vm[pq] += dx[n_pv_pq:]
        except np.linalg.LinAlgError:
            converged = False
            break

    if not converged:
        return {
            "status": "DIVERGED",
            "reason": "Solusi Newton-Raphson divergen (ketidakstabilan tegangan/reaktif pada island).",
            "iterations": actual_iters,
            "slack_bus": slack_node,
            "buses": {},
            "branches": [],
            "Ploss": 0.0
        }

    # Hitung daya slack dan aliran daya cabang
    P_calc = np.zeros(N)
    Q_calc = np.zeros(N)
    for i in range(N):
        for j in range(N):
            th_ij = Va[i] - Va[j]
            P_calc[i] += Vm[i] * Vm[j] * (G[i, j] * np.cos(th_ij) + B[i, j] * np.sin(th_ij))
            Q_calc[i] += Vm[i] * Vm[j] * (G[i, j] * np.sin(th_ij) - B[i, j] * np.cos(th_ij))

    slack_pd = buses[slack_node]['Pd'] * scale_load
    slack_qd = buses[slack_node]['Qd'] * scale_load
    P_slack_gen = (P_calc[slack_idx] * baseMVA) + slack_pd
    Q_slack_gen = (Q_calc[slack_idx] * baseMVA) + slack_qd

    res_buses = {}
    for i, b in enumerate(isl_nodes):
        pg = P_slack_gen if b == slack_node else buses[b]['Pg']
        qg = Q_slack_gen if b == slack_node else buses[b]['Qg']
        pd = buses[b]['Pd'] * scale_load
        qd = buses[b]['Qd'] * scale_load
        res_buses[b] = {
            "Vm": float(Vm[i]),
            "Va_deg": float(np.degrees(Va[i])),
            "Pd": float(pd),
            "Qd": float(qd),
            "Pg": float(pg),
            "Qg": float(qg),
            "Pmax": buses[b]['Pmax']
        }

    branch_results = []
    tot_ploss = 0.0
    for f, t, r, x, z, rateA, idx in sub_branches:
        i, j = node_to_idx[f], node_to_idx[t]
        Vi = Vm[i] * np.exp(1j * Va[i])
        Vj = Vm[j] * np.exp(1j * Va[j])
        y = 1.0 / complex(r, x)
        I_ij = (Vi - Vj) * y
        I_ji = (Vj - Vi) * y
        S_ij = Vi * np.conj(I_ij) * baseMVA
        S_ji = Vj * np.conj(I_ji) * baseMVA
        p_loss_br = S_ij.real + S_ji.real
        tot_ploss += max(0.0, p_loss_br)
        s_mag = max(abs(S_ij), abs(S_ji))
        loading_pct = (s_mag / rateA * 100.0) if rateA > 0 else 0.0
        branch_results.append({
            "idx": idx, "from": f, "to": t,
            "P_from": float(S_ij.real), "Q_from": float(S_ij.imag),
            "P_to": float(S_ji.real), "Q_to": float(S_ji.imag),
            "S_mva": float(s_mag),
            "loading_pct": float(loading_pct),
            "Ploss": float(p_loss_br)
        })

    status_str = "CONVERGED (UFLS)" if is_deficit else "CONVERGED"

    return {
        "status": status_str,
        "iterations": actual_iters,
        "slack_bus": slack_node,
        "is_deficit": is_deficit,
        "ufls_shed_mw": float(ufls_shed_mw),
        "buses": res_buses,
        "branches": branch_results,
        "Ploss": float(tot_ploss)
    }


def analyze_island_contingency(case_path, tripped_branch_indices):
    """
    Melakukan studi aliran daya lengkap untuk seluruh pulau (islands)
    yang terbentuk akibat pemutusan cabang-cabang tertentu.
    """
    buses, branches, baseMVA = parse_matpower(case_path)

    # Bangun topologi sistem tersisa
    G = nx.Graph()
    for b_id, b_data in buses.items():
        G.add_node(b_id, **b_data)

    tripped_set = set(tripped_branch_indices)
    for fbus, tbus, r, x, z, rateA, idx in branches:
        if idx not in tripped_set:
            G.add_edge(fbus, tbus, r=r, x=x, rateA=rateA, idx=idx)

    # Dekomposisi pulau
    island_components = list(nx.connected_components(G))
    # Urutkan berdasarkan jumlah bus terbanyak
    island_components.sort(key=lambda c: len(c), reverse=True)

    results = {
        "case_name": Path(case_path).name,
        "tripped_branches": tripped_branch_indices,
        "total_islands": len(island_components),
        "islands": []
    }

    total_survived_load = 0.0
    total_shed_load = 0.0

    for idx_isl, isl_nodes in enumerate(island_components, 1):
        isl_nodes = sorted(list(isl_nodes))
        sub_branches = [
            br for br in branches
            if br[6] not in tripped_set and br[0] in isl_nodes and br[1] in isl_nodes
        ]

        # Total beban nominal sebelum UFLS
        nominal_load_p = sum(buses[b]['Pd'] for b in isl_nodes)
        nominal_load_q = sum(buses[b]['Qd'] for b in isl_nodes)
        gen_nodes = [b for b in isl_nodes if buses[b]['has_gen']]
        total_gen_p = sum(buses[b]['Pg'] for b in isl_nodes)
        total_pmax = sum(buses[b]['Pmax'] for b in isl_nodes)

        # Hitung AC Power Flow
        pf_res = solve_ac_power_flow_island(buses, sub_branches, isl_nodes, baseMVA=baseMVA)

        status = pf_res["status"]
        if status in ["CONVERGED", "CONVERGED (UFLS)"]:
            shed = pf_res.get("ufls_shed_mw", 0.0)
            survived = nominal_load_p - shed
            total_survived_load += survived
            total_shed_load += shed
        else:
            total_shed_load += nominal_load_p

        island_data = {
            "island_id": idx_isl,
            "bus_count": len(isl_nodes),
            "buses": isl_nodes,
            "generator_buses": gen_nodes,
            "nominal_load_mw": nominal_load_p,
            "nominal_load_mvar": nominal_load_q,
            "installed_capacity_mw": total_pmax,
            "base_generation_mw": total_gen_p,
            "pf_result": pf_res
        }
        results["islands"].append(island_data)

    results["total_survived_load_mw"] = total_survived_load
    results["total_shed_load_mw"] = total_shed_load
    return results


def format_island_report(res, include_tables=True):
    """
    Format hasil studi aliran daya multi-island ke dalam laporan teks rapi
    standar ruang kendali transmisi (Dispatcher SCADA PLN).
    """
    lines = []
    is_multi = res['total_islands'] > 1
    title = "LAPORAN STUDI ALIRAN DAYA MULTI-ISLAND (SISTEM TERPISAH)" if is_multi else "LAPORAN DETAIL ALIRAN DAYA SISTEM (JARINGAN UTUH)"
    
    lines.append("=" * 88)
    lines.append(f"                 {title}")
    lines.append("=" * 88)
    lines.append(f"• Berkas Grid        : {res['case_name']}")
    lines.append(f"• Saluran Trip       : Saluran #{', #'.join(map(str, res['tripped_branches']))}")
    lines.append(f"• Jumlah Island      : {res['total_islands']} Pulau Kelistrikan Terbentuk")
    lines.append(f"• Total Beban Selamat: {res['total_survived_load_mw']:,.2f} MW ({res['total_survived_load_mw']/max(sum(isl['nominal_load_mw'] for isl in res['islands']), 1e-4)*100:.1f}%)")
    lines.append(f"• Total Beban Padam  : {res['total_shed_load_mw']:,.2f} MW ({res['total_shed_load_mw']/max(sum(isl['nominal_load_mw'] for isl in res['islands']), 1e-4)*100:.1f}% Loss-of-Load)")
    lines.append("-" * 88)

    # TABEL RANGKUMAN EKSEKUTIF SEMUA ISLAND (JIKA TERJADI PEMISAHAN SISTEM)
    if is_multi:
        lines.append("\n[ TABEL RANGKUMAN EKSEKUTIF KOMPARASI ANTAR-ISLAND ]")
        lines.append("+-----------+-------+-------------------+------------+------------+------------+------------+---------------+----------------+")
        lines.append("| Island ID | Jml B | Anggota Bus       | Beban (MW) | Kap Gen MW | Pg Aktif MW| Ploss (MW) | Local Slack   | Status Island  |")
        lines.append("+-----------+-------+-------------------+------------+------------+------------+------------+---------------+----------------+")

        tot_buses = sum(isl['bus_count'] for isl in res['islands'])
        tot_nominal_load = sum(isl['nominal_load_mw'] for isl in res['islands'])
        tot_cap = sum(isl['installed_capacity_mw'] for isl in res['islands'])
        tot_active_pg = 0.0
        tot_ploss = 0.0

        for isl in res["islands"]:
            i_id = isl["island_id"]
            n_b = isl["bus_count"]
            b_list = isl["buses"]
            if len(b_list) <= 3:
                b_str = ",".join(map(str, b_list))
            else:
                b_str = f"{b_list[0]}..{b_list[-1]} ({len(b_list)} bus)"

            p_load = isl["nominal_load_mw"]
            p_cap = isl["installed_capacity_mw"]
            pf = isl["pf_result"]
            st = pf["status"]

            if st in ["CONVERGED", "CONVERGED (UFLS)"]:
                pg = sum(b_data["Pg"] for b_data in pf["buses"].values())
                ploss = pf["Ploss"]
                tot_active_pg += pg
                tot_ploss += ploss
                slack_str = f"Bus #{pf['slack_bus']}"
                if n_b == 1 and p_load == 0:
                    st_badge = "🟢 IDLE (0 MW)"
                elif st == "CONVERGED":
                    st_badge = "🟢 STABIL"
                else:
                    st_badge = "🟡 UFLS SHED"
            elif st == "BLACKOUT":
                pg = 0.0
                ploss = 0.0
                slack_str = "TIDAK ADA"
                st_badge = "🔴 BLACKOUT"
            else:
                pg = 0.0
                ploss = 0.0
                slack_str = f"Bus #{pf.get('slack_bus', '-')}"
                st_badge = "⚪ DIVERGEN"

            lines.append(f"| Island #{i_id:1d} | {n_b:5d} | {b_str:17s} | {p_load:10.2f} | {p_cap:10.2f} | {pg:10.2f} | {ploss:10.2f} | {slack_str:13s} | {st_badge:14s} |")

        lines.append("+-----------+-------+-------------------+------------+------------+------------+------------+---------------+----------------+")
        pct_survived = res['total_survived_load_mw'] / max(tot_nominal_load, 1e-4) * 100.0
        lines.append(f"| TOTAL     | {tot_buses:5d} | -                 | {tot_nominal_load:10.2f} | {tot_cap:10.2f} | {tot_active_pg:10.2f} | {tot_ploss:10.2f} | -             | Selamat:{pct_survived:5.1f}% |")
        lines.append("+-----------+-------+-------------------+------------+------------+------------+------------+---------------+----------------+\n")

    for isl in res["islands"]:
        i_id = isl["island_id"]
        n_bus = isl["bus_count"]
        pf = isl["pf_result"]
        status = pf["status"]

        lines.append(f"\n▶ PULAU / ISLAND #{i_id}: {n_bus} Bus {isl['buses'][:10]}{'...' if n_bus > 10 else ''}")
        lines.append(f"  - Status Operasional  : {status}")
        lines.append(f"  - Beban Nominal Asli  : {isl['nominal_load_mw']:,.2f} MW | {isl['nominal_load_mvar']:,.2f} MVAr")
        lines.append(f"  - Total Kapasitas Gen : {isl['installed_capacity_mw']:,.2f} MW (Unit pada Bus: {isl['generator_buses'] or 'TIDAK ADA'})")

        if status == "BLACKOUT":
            lines.append("  - Dampak Kelistrikan  : 🔴 PADAM TOTAL (BLACKOUT).")
            lines.append(f"                          {pf.get('reason', 'Tidak ada pasokan generator.')}")
            lines.append(f"  - Pelepasan Beban     : 100% ({isl['nominal_load_mw']:,.2f} MW)")
            lines.append("  - Rekomendasi Operasi : Buka PMT beban. Siapkan prosedur Black-Start atau sinkronisasi pemulihan.")
        elif status == "DIVERGED":
            lines.append("  - Dampak Kelistrikan  : ⚠️ DIVERGEN (KOLAPS TEGANGAN).")
            lines.append(f"                          {pf.get('reason', '')}")
            lines.append("  - Rekomendasi Operasi : Segera lakukan pelepasan beban darurat manual (UFLS) dan kompensasi reaktif.")
        else:
            # CONVERGED
            slack_b = pf["slack_bus"]
            ploss = pf["Ploss"]
            tot_pg = sum(b_data["Pg"] for b_data in pf["buses"].values())
            tot_pd = sum(b_data["Pd"] for b_data in pf["buses"].values())

            lines.append(f"  - Local Slack Bus     : Bus #{slack_b} (Penyeimbang Daya & Frekuensi Island)")
            lines.append(f"  - Konvergensi NR      : Sukses ({pf.get('iterations', 0)} iterasi Newton-Raphson)")
            if pf.get("is_deficit"):
                lines.append(f"  - Tindakan UFLS       : ⚠️ Defisit Teratasi! Pelepasan Beban Otomatis: {pf['ufls_shed_mw']:,.2f} MW")
            lines.append(f"  - Neraca Daya Island  : Total Gen = {tot_pg:,.2f} MW | Total Beban = {tot_pd:,.2f} MW")
            lines.append(f"  - Rugi Transmisi      : {ploss:,.2f} MW ({(ploss/max(tot_pg, 1e-4))*100:.2f}%)")

            # Profil tegangan
            vm_list = [b_data["Vm"] for b_data in pf["buses"].values()]
            vmin, vmax = min(vm_list), max(vm_list)
            vmin_b = [b for b, d in pf["buses"].items() if abs(d["Vm"] - vmin) < 1e-4][0]
            vmax_b = [b for b, d in pf["buses"].items() if abs(d["Vm"] - vmax) < 1e-4][0]
            lines.append(f"  - Profil Tegangan     : Vmin = {vmin:.4f} p.u. (Bus #{vmin_b}) | Vmax = {vmax:.4f} p.u. (Bus #{vmax_b})")

            # TABEL TEGANGAN BUS LENGKAP
            if include_tables and pf["buses"]:
                lines.append("\n  [ PROFIL TEGANGAN & INJEKSI DAYA BUS ]")
                lines.append("  +-----+--------+----------+---------+---------+---------+---------+------------+")
                lines.append("  | Bus | V (pu) | Sudut(°) | Pg (MW) | Qg(MVA) | Pd (MW) | Qd(MVA) |   Status   |")
                lines.append("  +-----+--------+----------+---------+---------+---------+---------+------------+")
                for b in sorted(pf["buses"].keys()):
                    bd = pf["buses"][b]
                    v = bd["Vm"]
                    v_stat = "🔴 LOW V" if v < 0.90 else ("🟡 CAUTION" if v < 0.95 else ("🔴 HIGH V" if v > 1.10 else "🟢 NORMAL"))
                    if b == slack_b: v_stat = "⭐ SLACK"
                    lines.append(f"  | {b:3d} | {v:6.4f} | {bd['Va_deg']:8.2f} | {bd['Pg']:7.2f} | {bd['Qg']:7.2f} | {bd['Pd']:7.2f} | {bd['Qd']:7.2f} | {v_stat:10s} |")
                lines.append("  +-----+--------+----------+---------+---------+---------+---------+------------+")

            # TABEL ALIRAN DAYA SALURAN LENGKAP
            if include_tables and pf["branches"]:
                lines.append("\n  [ ALIRAN DAYA SALURAN TRANSMISI (BRANCH FLOWS) ]")
                lines.append("  +------+---------+---------+---------+---------+---------+---------+------------+")
                lines.append("  | Line | From➔To |  P (MW) | Q (MVA) | S (MVA) |  RateA  | Beban(%)|   Status   |")
                lines.append("  +------+---------+---------+---------+---------+---------+---------+------------+")
                for br in pf["branches"]:
                    pair = f"{br['from']}➔{br['to']}"
                    ld = br["loading_pct"]
                    b_stat = "🔴 OVERLOAD" if ld > 100.0 else ("🟡 HEAVY" if ld > 80.0 else "🟢 NORMAL")
                    rate_str = f"{br.get('rateA', 0):6.1f}" if br.get('rateA', 0) > 0 else "   -  "
                    ld_str = f"{ld:6.1f}%" if ld > 0 else "   -  "
                    lines.append(f"  | {br['idx']:4d} | {pair:7s} | {br['P_from']:7.2f} | {br['Q_from']:7.2f} | {br['S_mva']:7.2f} | {rate_str} | {ld_str} | {b_stat:10s} |")
                lines.append("  +------+---------+---------+---------+---------+---------+---------+------------+")

            lines.append("  - Rekomendasi Operasi : Pulau stabil. Monitor cadangan putar (spinning reserve) pada bus slack lokal.")

    lines.append("=" * 88)
    return "\n".join(lines)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Multi-Island Power Flow Study Engine")
    parser.add_argument("--case", type=str, default="data/case14.m", help="Path ke MATPOWER case file (.m)")
    parser.add_argument("--branches", type=str, default="1,2", help="Daftar index branch yang trip dipisah koma (misal: 1,2)")

    args = parser.parse_args()
    tripped = [int(x.strip()) for x in args.branches.split(",") if x.strip()]

    res = analyze_island_contingency(args.case, tripped)
    print(format_island_report(res))
