"""
GridGuard N-2: Platform Analisis & Skrining Kontingensi Cepat
Didesain untuk Insinyur Perencanaan & Operasi Sistem Tenaga Listrik (PLN / Grid Operator)
Tema: High-Contrast Modern SCADA Control Room (Dark Slate & Neon Status Badges)
Fitur: Interactive Zoomable & Pannable Network Topology Viewer (Impedance-Weighted Layout)
"""

import sys
import os
import json
import subprocess
from pathlib import Path

# Pastikan direktori root proyek selalu berada di sys.path agar modul 'scripts' dapat diimpor
if getattr(sys, 'frozen', False):
    base_candidates = []
    if hasattr(sys, '_MEIPASS'):
        base_candidates.append(Path(sys._MEIPASS).resolve())
    exec_dir = Path(sys.executable).resolve().parent
    base_candidates.append(exec_dir)
    base_candidates.append(exec_dir / "_internal")
    base_candidates.append(exec_dir.parent / "Resources")
    
    PROJECT_ROOT = base_candidates[0]
    for c in base_candidates:
        if (c / "data").exists() or (c / "assets").exists():
            PROJECT_ROOT = c
            break
    for c in base_candidates:
        if str(c) not in sys.path:
            sys.path.insert(0, str(c))
else:
    PROJECT_ROOT = Path(__file__).resolve().parent.parent
    if str(PROJECT_ROOT) not in sys.path:
        sys.path.insert(0, str(PROJECT_ROOT))

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QLabel, QPushButton, QFileDialog, QComboBox,
    QSpinBox, QSlider, QTabWidget, QTableWidget, QTableWidgetItem,
    QHeaderView, QTextEdit, QProgressBar, QGroupBox, QSplitter,
    QMessageBox, QLineEdit, QRadioButton, QButtonGroup, QFrame,
    QScrollArea, QGraphicsView, QGraphicsScene, QGraphicsPixmapItem,
    QStackedWidget
)
from PySide6.QtCore import Qt, QThread, Signal, QRectF, QUrl
from PySide6.QtGui import QFont, QColor, QBrush, QPixmap, QPainter, QDesktopServices

try:
    from PySide6.QtWebEngineWidgets import QWebEngineView
    HAS_WEBENGINE = True
except Exception:
    HAS_WEBENGINE = False


class ZoomableTopologyView(QGraphicsView):
    """
    Kanvas interaktif untuk melihat peta topologi jaringan listrik.
    Mendukung Zoom In / Zoom Out dengan scroll mouse atau pinch trackpad,
    serta Drag / Pan untuk menggeser jaringan tanpa batas.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        self.pixmap_item = None

        self.setRenderHints(QPainter.Antialiasing | QPainter.SmoothPixmapTransform)
        self.setDragMode(QGraphicsView.ScrollHandDrag)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.AnchorUnderMouse)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.setStyleSheet("""
            background-color: #030712;
            border: 1px solid #1f2937;
            border-radius: 8px;
        """)

    def set_image(self, pixmap):
        self.scene.clear()
        self.resetTransform()
        if not pixmap.isNull():
            self.pixmap_item = self.scene.addPixmap(pixmap)
            self.scene.setSceneRect(self.pixmap_item.boundingRect())
            self.fit_in_view()

    def fit_in_view(self):
        if self.pixmap_item:
            self.fitInView(self.pixmap_item, Qt.KeepAspectRatio)

    def zoom_in(self):
        self.scale(1.25, 1.25)

    def zoom_out(self):
        self.scale(0.8, 0.8)

    def reset_actual_size(self):
        self.resetTransform()

    def wheelEvent(self, event):
        # Zoom halus menggunakan roda mouse atau gesture cubit trackpad Mac
        if event.angleDelta().y() > 0:
            factor = 1.15
        else:
            factor = 0.87
        self.scale(factor, factor)
        event.accept()


class SimulationWorker(QThread):
    finished_signal = Signal(bool, str, dict)
    log_signal = Signal(str)

    def __init__(self, binary_path, case_path, threads, limit, force_all):
        super().__init__()
        self.binary_path = binary_path
        self.case_path = case_path
        self.threads = threads
        self.limit = limit
        self.force_all = force_all
        self.is_running = True
        self.process = None

    def run(self):
        import tempfile
        json_tmp = Path(tempfile.gettempdir()) / f"gridguard_result_{os.getpid()}_{id(self)}.json"
        cmd = [
            str(self.binary_path),
            str(self.case_path),
            "--threads", str(self.threads),
            "--json-out", str(json_tmp)
        ]
        if self.force_all:
            cmd.append("--all")
        elif self.limit > 0:
            cmd.extend(["--limit", str(self.limit)])

        self.log_signal.emit(f"🚀 Menjalankan C++20 Woodbury Engine:\n{' '.join(cmd)}\n")

        try:
            popen_kwargs = {
                "stdout": subprocess.PIPE,
                "stderr": subprocess.STDOUT,
                "text": True,
                "bufsize": 1
            }
            if sys.platform.startswith("win"):
                popen_kwargs["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)

            self.process = subprocess.Popen(cmd, **popen_kwargs)

            for line in iter(self.process.stdout.readline, ''):
                if not self.is_running:
                    self.process.terminate()
                    break
                self.log_signal.emit(line)

            self.process.wait()

            if json_tmp.exists():
                try:
                    with open(json_tmp, "r") as f:
                        data = json.load(f)
                    json_tmp.unlink(missing_ok=True)
                    self.finished_signal.emit(True, "Simulasi Selesai.", data)
                    return
                except Exception as e:
                    self.finished_signal.emit(False, f"Gagal membaca JSON: {e}", {})
                    return

            self.finished_signal.emit(False, "Berkas output JSON tidak ditemukan.", {})

        except Exception as e:
            self.finished_signal.emit(False, str(e), {})

    def stop(self):
        self.is_running = False
        if self.process:
            self.process.terminate()


class GridGuardMainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("GridGuard N-2 — [Sistem Skrining Kontingensi Cepat - PLN / Operator Grid]")
        self.resize(1340, 880)
        self.setMinimumSize(1080, 720)

        self.project_root = PROJECT_ROOT
        exec_dir = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else self.project_root

        # 1. Tentukan direktori data (berisi berkas MATPOWER case .m)
        self.data_dir = self.project_root / "data"
        data_candidates = [
            self.project_root / "data",
            exec_dir / "data",
            exec_dir / "_internal" / "data",
            (Path(sys._MEIPASS).resolve() / "data") if hasattr(sys, '_MEIPASS') else None,
        ]
        for dc in data_candidates:
            if dc and dc.is_dir() and ((dc / "case14.m").exists() or (dc / "case57.m").exists()):
                self.data_dir = dc
                break

        # 2. Tentukan direktori assets
        self.assets_dir = self.project_root / "assets"
        assets_candidates = [
            self.project_root / "assets",
            exec_dir / "assets",
            exec_dir / "_internal" / "assets",
            (Path(sys._MEIPASS).resolve() / "assets") if hasattr(sys, '_MEIPASS') else None,
        ]
        for ac in assets_candidates:
            if ac and ac.is_dir():
                self.assets_dir = ac
                break

        # 3. Temukan executable mesin komputasi C++20 Woodbury Engine
        # Di Windows: contingency_n2_woodbury.exe; Di macOS/Linux: contingency_n2_woodbury
        bin_names = (
            ["contingency_n2_woodbury.exe", "contingency_n2_woodbury"]
            if sys.platform.startswith("win")
            else ["contingency_n2_woodbury", "contingency_n2_woodbury.exe"]
        )

        search_dirs = [
            self.project_root / "build",
            self.project_root / "build" / "Release",
            self.project_root,
            exec_dir / "build",
            exec_dir / "build" / "Release",
            exec_dir,
            exec_dir / "_internal" / "build",
            exec_dir / "_internal" / "build" / "Release",
            exec_dir / "_internal",
        ]
        if hasattr(sys, '_MEIPASS'):
            meipass = Path(sys._MEIPASS).resolve()
            search_dirs.extend([
                meipass / "build",
                meipass / "build" / "Release",
                meipass,
            ])

        self.binary_path = None
        for sdir in search_dirs:
            for bname in bin_names:
                cand = sdir / bname
                if cand.is_file() and cand.exists():
                    self.binary_path = cand
                    break
            if self.binary_path:
                break

        if not self.binary_path:
            self.binary_path = self.project_root / "build" / bin_names[0]

        if self.binary_path.exists() and not sys.platform.startswith("win"):
            if not os.access(self.binary_path, os.X_OK):
                try:
                    os.chmod(self.binary_path, 0o755)
                except Exception:
                    pass

        # 4. Berkas case awal
        self.current_case_path = self.data_dir / "case14.m"
        for cname in ["case14.m", "case57.m", "case118.m", "case30.m", "case300.m"]:
            cand = self.data_dir / cname
            if cand.exists():
                self.current_case_path = cand
                break

        self.current_results = None
        self.worker = None
        self.current_topo_img_path = self.assets_dir / f"topology_{self.current_case_path.stem}_impedance.png"
        self.current_topo_html_path = self.assets_dir / f"topology_{self.current_case_path.stem}_interactive.html"

        self.setup_ui()
        self.apply_high_contrast_theme()

        # Load topologi awal jika ada
        self.load_topology_views(self.current_topo_html_path, self.current_topo_img_path)

    def setup_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setContentsMargins(14, 14, 14, 14)
        main_layout.setSpacing(12)

        # ---------------- HEADER TITLE BANNER ----------------
        header = QFrame()
        header.setObjectName("HeaderFrame")
        h_layout = QHBoxLayout(header)
        h_layout.setContentsMargins(18, 12, 18, 12)

        v_title = QVBoxLayout()
        title_label = QLabel("⚡ GridGuard N-2  |  Power System Security & Contingency Analyzer")
        title_label.setFont(QFont("Segoe UI", 15, QFont.Bold))
        title_label.setObjectName("MainTitle")
        
        subtitle_label = QLabel("Pusat Pengatur Beban & Perencanaan Sistem Transmisi • C++20 Woodbury Engine")
        subtitle_label.setObjectName("SubTitle")
        
        v_title.addWidget(title_label)
        v_title.addWidget(subtitle_label)
        h_layout.addLayout(v_title)
        h_layout.addStretch()

        self.btn_export_excel = QPushButton("📊 Ekspor Excel (.xlsx)")
        self.btn_export_excel.setObjectName("BtnExport")
        self.btn_export_excel.setEnabled(False)
        self.btn_export_excel.clicked.connect(self.export_excel)
        h_layout.addWidget(self.btn_export_excel)

        self.btn_export_json = QPushButton("💾 Simpan JSON")
        self.btn_export_json.setObjectName("BtnExport")
        self.btn_export_json.setEnabled(False)
        self.btn_export_json.clicked.connect(self.export_json_action)
        h_layout.addWidget(self.btn_export_json)

        main_layout.addWidget(header)

        # ---------------- SPLITTER LAYOUT (LEFT: CONFIG, RIGHT: WORKSPACE) ----------------
        splitter = QSplitter(Qt.Horizontal)

        # ---------------- SISI KIRI: PANEL PARAMETER ----------------
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setContentsMargins(0, 0, 10, 0)
        left_layout.setSpacing(12)

        # Group 1: Data Grid
        grp_grid = QGroupBox("1. DATA JARINGAN TRANSMISI")
        v_grid = QVBoxLayout(grp_grid)
        v_grid.setSpacing(8)

        self.lbl_case_name = QLabel(f"Berkas: {self.current_case_path.name}")
        self.lbl_case_name.setObjectName("FileNameBadge")
        btn_browse = QPushButton("📁 Pilih Kasus Grid (.m)")
        btn_browse.setObjectName("BtnBrowse")
        btn_browse.clicked.connect(self.browse_file)
        v_grid.addWidget(self.lbl_case_name)
        v_grid.addWidget(btn_browse)

        self.lbl_grid_buses = QLabel("• Jumlah Bus      : -")
        self.lbl_grid_branches = QLabel("• Jumlah Saluran  : -")
        self.lbl_grid_load = QLabel("• Beban Total     : - MW")
        self.lbl_grid_losses = QLabel("• Rugi Base-Case  : - MW")
        for lbl in [self.lbl_grid_buses, self.lbl_grid_branches, self.lbl_grid_load, self.lbl_grid_losses]:
            lbl.setObjectName("GridInfoText")
            v_grid.addWidget(lbl)

        btn_view_topo = QPushButton("🗺️ Peta Topologi (Zoom & Pan)")
        btn_view_topo.setObjectName("BtnTopoShortcut")
        btn_view_topo.clicked.connect(lambda: self.tabs.setCurrentIndex(2))
        v_grid.addWidget(btn_view_topo)

        left_layout.addWidget(grp_grid)

        # Group 2: Parameter Kontingensi
        grp_param = QGroupBox("2. SKUP EVALUASI KONTINGENSI")
        v_param = QVBoxLayout(grp_param)
        v_param.setSpacing(8)

        self.rb_n2 = QRadioButton("Skrining N-2 (Trip 2 Saluran)")
        self.rb_n2.setChecked(True)
        v_param.addWidget(self.rb_n2)

        lbl_limit = QLabel("Batas Jumlah Skenario:")
        self.spin_limit = QSpinBox()
        self.spin_limit.setRange(10, 10000000)
        self.spin_limit.setValue(10000)
        self.spin_limit.setSingleStep(1000)
        v_param.addWidget(lbl_limit)
        v_param.addWidget(self.spin_limit)

        self.chk_all = QRadioButton("Simulasi Seluruh Pasangan (Exhaustive)")
        v_param.addWidget(self.chk_all)
        left_layout.addWidget(grp_param)

        # Group 3: Hardware Accelerator
        grp_hw = QGroupBox("3. AKSELERASI HARDWARE (C++20)")
        v_hw = QVBoxLayout(grp_hw)
        v_hw.setSpacing(8)

        max_cpu = os.cpu_count() or 4
        self.lbl_threads = QLabel(f"Alokasi Core CPU: {max_cpu} Core")
        self.slider_threads = QSlider(Qt.Horizontal)
        self.slider_threads.setRange(1, max_cpu)
        self.slider_threads.setValue(max_cpu)
        self.slider_threads.valueChanged.connect(
            lambda v: self.lbl_threads.setText(f"Alokasi Core CPU: {v} Core")
        )
        v_hw.addWidget(self.lbl_threads)
        v_hw.addWidget(self.slider_threads)
        left_layout.addWidget(grp_hw)

        # Action Buttons
        self.btn_run = QPushButton("▶ JALANKAN SKRINING N-2")
        self.btn_run.setObjectName("RunButton")
        self.btn_run.setFixedHeight(48)
        self.btn_run.clicked.connect(self.start_simulation)
        left_layout.addWidget(self.btn_run)

        self.btn_stop = QPushButton("⏹ Hentikan Simulasi")
        self.btn_stop.setObjectName("StopButton")
        self.btn_stop.setEnabled(False)
        self.btn_stop.setFixedHeight(36)
        self.btn_stop.clicked.connect(self.stop_simulation)
        left_layout.addWidget(self.btn_stop)

        left_layout.addStretch()
        left_panel.setFixedWidth(310)
        splitter.addWidget(left_panel)

        # ---------------- SISI KANAN: WORKSPACE TABBED ----------------
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(10, 0, 0, 0)

        self.tabs = QTabWidget()

        # TAB 1: Matriks Pemeringkatan
        tab_matrix = QWidget()
        v_matrix = QVBoxLayout(tab_matrix)
        v_matrix.setContentsMargins(8, 10, 8, 8)
        v_matrix.setSpacing(10)

        # Filter bar
        filter_layout = QHBoxLayout()
        filter_layout.addWidget(QLabel("🔍 Cari Gardu/Bus:"))
        self.txt_search = QLineEdit()
        self.txt_search.setPlaceholderText("Ketik nomor bus (contoh: 1, 6036, 7351)...")
        self.txt_search.textChanged.connect(self.filter_table)
        filter_layout.addWidget(self.txt_search)

        filter_layout.addWidget(QLabel("Filter Status:"))
        self.cmb_status = QComboBox()
        self.cmb_status.addItems(["SEMUA STATUS", "🔴 Hanya Kritis / Overload", "🟡 Hanya Islanding", "⚪ Hanya Divergen"])
        self.cmb_status.currentIndexChanged.connect(self.filter_table)
        filter_layout.addWidget(self.cmb_status)
        v_matrix.addLayout(filter_layout)

        # Table
        self.table = QTableWidget()
        self.table.setColumnCount(9)
        self.table.setHorizontalHeaderLabels([
            "Rank", "Pasangan Trip", "Saluran 1 (From➔To)", "Saluran 2 (From➔To)",
            "Status Sistem", "Max Load (%)", "Overload", "Vmin (Bus)", "PI Total"
        ])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.itemSelectionChanged.connect(self.on_table_row_selected)
        self.table.itemDoubleClicked.connect(lambda: self.tabs.setCurrentIndex(1))
        v_matrix.addWidget(self.table)

        btn_inspect_bar = QHBoxLayout()
        self.btn_inspect_detail = QPushButton("🔍 Buka Laporan Detail Aliran Daya Kontingensi Terpilih di Tab 2 ➔")
        self.btn_inspect_detail.setStyleSheet("background-color: #0284c7; border-color: #38bdf8; font-weight: bold; padding: 7px 14px;")
        self.btn_inspect_detail.clicked.connect(lambda: self.tabs.setCurrentIndex(1))
        btn_inspect_bar.addWidget(self.btn_inspect_detail)
        btn_inspect_bar.addStretch()
        v_matrix.addLayout(btn_inspect_bar)

        self.tabs.addTab(tab_matrix, "📋 Matriks Pemeringkatan Kontingensi")

        # TAB 2: Detail Inspeksi & Aliran Daya (Power Flow)
        tab_detail = QWidget()
        v_detail = QVBoxLayout(tab_detail)
        v_detail.setContentsMargins(10, 10, 10, 10)
        v_detail.setSpacing(8)

        header_detail_bar = QHBoxLayout()
        self.lbl_detail_title = QLabel("Pilih baris kontingensi pada Tabel di Tab 1 untuk melihat detail.")
        self.lbl_detail_title.setFont(QFont("Segoe UI", 12, QFont.Bold))
        self.lbl_detail_title.setObjectName("DetailHeader")
        header_detail_bar.addWidget(self.lbl_detail_title)
        header_detail_bar.addStretch()

        self.btn_copy_detail = QPushButton("📋 Salin Laporan")
        self.btn_copy_detail.clicked.connect(self.copy_detail_report)
        header_detail_bar.addWidget(self.btn_copy_detail)

        self.btn_save_detail = QPushButton("💾 Simpan Teks (.txt)")
        self.btn_save_detail.clicked.connect(self.save_detail_report)
        header_detail_bar.addWidget(self.btn_save_detail)

        v_detail.addLayout(header_detail_bar)

        self.txt_detail = QTextEdit()
        self.txt_detail.setReadOnly(True)
        self.txt_detail.setFont(QFont("Menlo", 10.5))
        self.txt_detail.setObjectName("DetailBox")
        v_detail.addWidget(self.txt_detail)

        self.tabs.addTab(tab_detail, "🔍 Detail Inspeksi & Aliran Daya")

        # TAB 3: Visualisasi Topologi Jaringan Interaktif (Bebas Tumpang Tindih)
        tab_topo = QWidget()
        v_topo = QVBoxLayout(tab_topo)
        v_topo.setContentsMargins(10, 10, 10, 10)
        v_topo.setSpacing(8)

        # Toolbar Kontrol Interaktif
        topo_bar = QHBoxLayout()
        topo_bar.setSpacing(8)

        self.btn_render_topo = QPushButton("🔄 Render Ulang Topologi")
        self.btn_render_topo.setObjectName("BtnExport")
        self.btn_render_topo.clicked.connect(self.generate_and_show_topology)
        topo_bar.addWidget(self.btn_render_topo)

        self.btn_toggle_topo_mode = QPushButton("🖼️ Beralih ke Gambar Statis (PNG)")
        self.btn_toggle_topo_mode.clicked.connect(self.toggle_topology_mode)
        topo_bar.addWidget(self.btn_toggle_topo_mode)

        self.btn_zoom_in = QPushButton("➕ Zoom In")
        self.btn_zoom_in.clicked.connect(self.zoom_in_topology)
        topo_bar.addWidget(self.btn_zoom_in)

        self.btn_zoom_out = QPushButton("➖ Zoom Out")
        self.btn_zoom_out.clicked.connect(self.zoom_out_topology)
        topo_bar.addWidget(self.btn_zoom_out)

        self.btn_fit_view = QPushButton("⛶ Fit Layar")
        self.btn_fit_view.clicked.connect(self.fit_topology_view)
        topo_bar.addWidget(self.btn_fit_view)

        self.btn_open_external_topo = QPushButton("🌐 Buka di Browser Penuh")
        self.btn_open_external_topo.setStyleSheet("background-color: #0369a1; border-color: #38bdf8; color: #ffffff;")
        self.btn_open_external_topo.clicked.connect(self.open_external_topology)
        topo_bar.addWidget(self.btn_open_external_topo)

        topo_bar.addStretch()

        self.lbl_topo_info = QLabel("💡 Geser (drag) sembarang bus untuk merapikan • Scroll/Pinch: Zoom • Hover: Cek MW & Z")
        self.lbl_topo_info.setStyleSheet("color: #38bdf8; font-weight: 500; font-size: 11px;")
        topo_bar.addWidget(self.lbl_topo_info)
        v_topo.addLayout(topo_bar)

        # QStackedWidget: Page 0 = Interactive Web (Vis.js Physics), Page 1 = Static High-Res PNG
        self.stack_topo = QStackedWidget()

        if HAS_WEBENGINE:
            self.web_topo = QWebEngineView()
            self.web_topo.setStyleSheet("background-color: #030712; border: 1px solid #1f2937; border-radius: 8px;")
            self.stack_topo.addWidget(self.web_topo)
        else:
            self.web_topo = None
            self.btn_toggle_topo_mode.setEnabled(False)
            self.btn_toggle_topo_mode.setText("Mode Statis (WebEngine N/A)")

        self.view_topo = ZoomableTopologyView()
        self.stack_topo.addWidget(self.view_topo)

        v_topo.addWidget(self.stack_topo)

        self.tabs.addTab(tab_topo, "🗺️ Peta Topologi Jaringan (Interaktif & Bebas Overlap)")

        # TAB 4: Log Eksekusi & Hardware Profiling
        tab_log = QWidget()
        v_log = QVBoxLayout(tab_log)
        v_log.setContentsMargins(10, 10, 10, 10)

        metric_box = QHBoxLayout()
        metric_box.setSpacing(12)

        self.card_solves = QFrame()
        self.card_solves.setObjectName("KpiCardSolves")
        c1 = QVBoxLayout(self.card_solves)
        self.lbl_val_solves = QLabel("—")
        self.lbl_val_solves.setFont(QFont("Segoe UI", 20, QFont.Bold))
        self.lbl_val_solves.setAlignment(Qt.AlignCenter)
        lbl_title_solves = QLabel("THROUGHPUT ENGINE\n(Solves / Detik)")
        lbl_title_solves.setAlignment(Qt.AlignCenter)
        c1.addWidget(self.lbl_val_solves)
        c1.addWidget(lbl_title_solves)
        metric_box.addWidget(self.card_solves)

        self.card_time = QFrame()
        self.card_time.setObjectName("KpiCardTime")
        c2 = QVBoxLayout(self.card_time)
        self.lbl_val_time = QLabel("—")
        self.lbl_val_time.setFont(QFont("Segoe UI", 20, QFont.Bold))
        self.lbl_val_time.setAlignment(Qt.AlignCenter)
        lbl_title_time = QLabel("TOTAL WALL-CLOCK\n(Milidetik)")
        lbl_title_time.setAlignment(Qt.AlignCenter)
        c2.addWidget(self.lbl_val_time)
        c2.addWidget(lbl_title_time)
        metric_box.addWidget(self.card_time)

        self.card_avg = QFrame()
        self.card_avg.setObjectName("KpiCardAvg")
        c3 = QVBoxLayout(self.card_avg)
        self.lbl_val_avg = QLabel("—")
        self.lbl_val_avg.setFont(QFont("Segoe UI", 20, QFont.Bold))
        self.lbl_val_avg.setAlignment(Qt.AlignCenter)
        lbl_title_avg = QLabel("RATA-RATA WAKTU\n(Mikrodetik / Kasus)")
        lbl_title_avg.setAlignment(Qt.AlignCenter)
        c3.addWidget(self.lbl_val_avg)
        c3.addWidget(lbl_title_avg)
        metric_box.addWidget(self.card_avg)

        v_log.addLayout(metric_box)

        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setFont(QFont("Menlo", 10))
        self.txt_log.setObjectName("ConsoleBox")
        v_log.addWidget(self.txt_log)

        self.tabs.addTab(tab_log, "⚙️ Log Komputasi & Profiling Hardware")

        right_layout.addWidget(self.tabs)
        splitter.addWidget(right_panel)
        main_layout.addWidget(splitter)

        # ---------------- STATUS BAR BAWAH ----------------
        status_bar = QHBoxLayout()
        self.lbl_status = QLabel("Status: Siap.")
        self.lbl_status.setObjectName("StatusText")

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setTextVisible(False)
        self.progress_bar.setFixedWidth(220)

        status_bar.addWidget(self.lbl_status)
        status_bar.addStretch()
        status_bar.addWidget(self.progress_bar)
        main_layout.addLayout(status_bar)

    def apply_high_contrast_theme(self):
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #0b1120;
                color: #f1f5f9;
                font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, 'Helvetica Neue', sans-serif;
                font-size: 12px;
            }
            #HeaderFrame {
                background-color: #1e293b;
                border: 1px solid #334155;
                border-radius: 8px;
            }
            #MainTitle {
                color: #38bdf8;
                font-size: 16px;
                font-weight: bold;
            }
            #SubTitle {
                color: #94a3b8;
                font-size: 11px;
            }
            QGroupBox {
                background-color: #111827;
                border: 1px solid #1f2937;
                border-radius: 8px;
                margin-top: 14px;
                padding-top: 14px;
                font-weight: bold;
                color: #38bdf8;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 12px;
                padding: 0 6px;
                color: #38bdf8;
                font-size: 11px;
            }
            QLabel {
                color: #cbd5e1;
            }
            #FileNameBadge {
                background-color: #1e293b;
                color: #f8fafc;
                font-weight: bold;
                padding: 6px;
                border-radius: 4px;
                border: 1px solid #334155;
            }
            #GridInfoText {
                color: #94a3b8;
                font-size: 11px;
            }
            QPushButton {
                background-color: #1e293b;
                color: #f8fafc;
                border: 1px solid #475569;
                border-radius: 6px;
                padding: 6px 12px;
                font-weight: 600;
            }
            QPushButton:hover {
                background-color: #334155;
                border-color: #64748b;
            }
            QPushButton:pressed {
                background-color: #0f172a;
            }
            QPushButton:disabled {
                background-color: #1e293b;
                color: #475569;
                border-color: #334155;
            }
            #BtnExport, #BtnTopoShortcut {
                background-color: #0f766e;
                border: 1px solid #14b8a6;
                color: #f0fdfa;
            }
            #BtnExport:hover, #BtnTopoShortcut:hover {
                background-color: #115e59;
            }
            #RunButton {
                background-color: #059669;
                color: #ffffff;
                font-size: 14px;
                font-weight: bold;
                border: 1px solid #10b981;
                border-radius: 6px;
            }
            #RunButton:hover {
                background-color: #047857;
            }
            #RunButton:disabled {
                background-color: #1f2937;
                color: #4b5563;
                border-color: #374151;
            }
            #StopButton {
                background-color: #7f1d1d;
                color: #fecaca;
                border: 1px solid #dc2626;
            }
            #StopButton:hover {
                background-color: #991b1b;
            }
            #StopButton:disabled {
                background-color: #1f2937;
                color: #4b5563;
                border-color: #374151;
            }
            QLineEdit, QSpinBox, QComboBox {
                background-color: #0f172a;
                color: #f8fafc;
                border: 1px solid #334155;
                border-radius: 6px;
                padding: 6px;
                selection-background-color: #0284c7;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border: 1px solid #38bdf8;
            }
            QComboBox QAbstractItemView {
                background-color: #0f172a;
                color: #f8fafc;
                selection-background-color: #0284c7;
            }
            QTabWidget::pane {
                border: 1px solid #1e293b;
                background-color: #111827;
                border-radius: 8px;
            }
            QTabBar::tab {
                background-color: #0f172a;
                color: #94a3b8;
                padding: 8px 16px;
                border-top-left-radius: 6px;
                border-top-right-radius: 6px;
                margin-right: 4px;
                font-weight: 600;
            }
            QTabBar::tab:selected {
                background-color: #111827;
                color: #38bdf8;
                border-bottom: 2px solid #38bdf8;
            }
            QTableWidget {
                background-color: #0f172a;
                alternate-background-color: #131d31;
                color: #f8fafc;
                gridline-color: #1e293b;
                border: 1px solid #1e293b;
                border-radius: 6px;
                selection-background-color: #0284c7;
                selection-color: #ffffff;
            }
            QHeaderView::section {
                background-color: #1e293b;
                color: #e2e8f0;
                font-weight: bold;
                font-size: 11px;
                padding: 8px;
                border: 1px solid #0f172a;
            }
            #DetailHeader {
                color: #38bdf8;
            }
            #DetailBox, #ConsoleBox {
                background-color: #030712;
                color: #e2e8f0;
                border: 1px solid #1f2937;
                border-radius: 6px;
                padding: 10px;
            }
            #KpiCardSolves {
                background-color: #082f49;
                border: 1px solid #0284c7;
                border-radius: 8px;
                color: #38bdf8;
            }
            #KpiCardTime {
                background-color: #064e3b;
                border: 1px solid #059669;
                border-radius: 8px;
                color: #34d399;
            }
            #KpiCardAvg {
                background-color: #4c1d95;
                border: 1px solid #7c3aed;
                border-radius: 8px;
                color: #c084fc;
            }
            QProgressBar {
                background-color: #1e293b;
                border: 1px solid #334155;
                border-radius: 4px;
                height: 12px;
            }
            QProgressBar::chunk {
                background-color: #10b981;
                border-radius: 3px;
            }
            #StatusText {
                color: #94a3b8;
                font-weight: 500;
            }
        """)

    def browse_file(self):
        start_dir = str(self.data_dir) if self.data_dir.exists() else str(self.project_root / "data")
        fpath, _ = QFileDialog.getOpenFileName(
            self, "Pilih Berkas MATPOWER Case", start_dir, "MATPOWER Case (*.m)"
        )
        if fpath:
            self.current_case_path = Path(fpath)
            self.lbl_case_name.setText(f"Berkas: {self.current_case_path.name}")
            expected_img = self.assets_dir / f"topology_{self.current_case_path.stem}_impedance.png"
            expected_html = self.assets_dir / f"topology_{self.current_case_path.stem}_interactive.html"
            self.current_topo_img_path = expected_img
            self.current_topo_html_path = expected_html
            if expected_img.exists() and expected_html.exists():
                self.load_topology_views(expected_html, expected_img)
            else:
                self.generate_and_show_topology()

    def generate_and_show_topology(self):
        assets_dir = self.assets_dir
        try:
            assets_dir.mkdir(parents=True, exist_ok=True)
            test_file = assets_dir / f".test_write_{os.getpid()}"
            test_file.touch()
            test_file.unlink(missing_ok=True)
            writable_cache = assets_dir
        except Exception:
            if sys.platform.startswith("win"):
                local_app_data = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
                writable_cache = Path(local_app_data) / "GridGuard" / "assets"
            elif sys.platform == "darwin":
                writable_cache = Path.home() / "Library" / "Caches" / "GridGuard" / "assets"
            else:
                writable_cache = Path.home() / ".cache" / "GridGuard" / "assets"
            writable_cache.mkdir(parents=True, exist_ok=True)

        out_img = writable_cache / f"topology_{self.current_case_path.stem}_impedance.png"
        out_html = writable_cache / f"topology_{self.current_case_path.stem}_interactive.html"
        self.lbl_topo_info.setText("⏳ Sedang menghitung layout bebas overlap (Collision-Relaxation & Physics)...")
        QApplication.processEvents()

        try:
            for p in [str(self.project_root), str(self.project_root.parent)]:
                if p not in sys.path:
                    sys.path.insert(0, p)
            from scripts.plot_network_topology import plot_topology_impedance, export_interactive_html
            plot_topology_impedance(str(self.current_case_path), str(out_img))
            export_interactive_html(str(self.current_case_path), str(out_html))

            self.current_topo_img_path = out_img
            self.current_topo_html_path = out_html
            self.load_topology_views(out_html, out_img)
            self.lbl_topo_info.setText(f"✓ Topologi {self.current_case_path.name} berhasil dimuat: Bebas tumpang tindih (Drag & Zoom aktif).")
        except Exception as e:
            QMessageBox.warning(self, "Peringatan", f"Gagal merender topologi: {e}\n(Catatan: Untuk sistem masif >300 bus, layout graf impedansi membutuhkan komputasi lebih lama)")
            self.lbl_topo_info.setText("Gagal merender topologi.")

    def load_topology_views(self, html_path, img_path):
        if HAS_WEBENGINE and self.web_topo and Path(html_path).exists():
            self.web_topo.load(QUrl.fromLocalFile(str(html_path)))
        if Path(img_path).exists():
            pixmap = QPixmap(str(img_path))
            if not pixmap.isNull():
                self.view_topo.set_image(pixmap)

    def load_topology_image(self, img_path):
        if Path(img_path).exists():
            pixmap = QPixmap(str(img_path))
            if not pixmap.isNull():
                self.view_topo.set_image(pixmap)

    def toggle_topology_mode(self):
        if not HAS_WEBENGINE or not self.web_topo:
            QMessageBox.information(self, "Info", "QtWebEngine tidak tersedia pada sistem ini. Menampilkan gambar statis resolusi tinggi.")
            return

        current_idx = self.stack_topo.currentIndex()
        if current_idx == 0:
            self.stack_topo.setCurrentIndex(1)
            self.btn_toggle_topo_mode.setText("⚡ Beralih ke Mode Fisika Interaktif")
            self.lbl_topo_info.setText("💡 Mode Statis Bebas Tabrakan: Scroll Mouse/Trackpad: Zoom | Klik & Geser: Pan")
        else:
            self.stack_topo.setCurrentIndex(0)
            self.btn_toggle_topo_mode.setText("🖼️ Beralih ke Gambar Statis (PNG)")
            self.lbl_topo_info.setText("💡 Mode Interaktif: Drag sembarang bus untuk merapikan • Scroll: Zoom • Hover: Cek MW & Z")

    def zoom_in_topology(self):
        if self.stack_topo.currentIndex() == 0 and self.web_topo:
            self.web_topo.setZoomFactor(self.web_topo.zoomFactor() * 1.25)
        else:
            self.view_topo.zoom_in()

    def zoom_out_topology(self):
        if self.stack_topo.currentIndex() == 0 and self.web_topo:
            self.web_topo.setZoomFactor(self.web_topo.zoomFactor() * 0.8)
        else:
            self.view_topo.zoom_out()

    def fit_topology_view(self):
        if self.stack_topo.currentIndex() == 0 and self.web_topo:
            self.web_topo.setZoomFactor(1.0)
            self.web_topo.page().runJavaScript("if (window.network) network.fit({animation: true});")
        else:
            self.view_topo.fit_in_view()

    def open_external_topology(self):
        target_html = getattr(self, "current_topo_html_path", None)
        if not target_html or not Path(target_html).exists():
            target_html = self.assets_dir / f"topology_{self.current_case_path.stem}_interactive.html"

        if Path(target_html).exists():
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(target_html)))
        else:
            target_img = getattr(self, "current_topo_img_path", None)
            if not target_img or not Path(target_img).exists():
                target_img = self.assets_dir / f"topology_{self.current_case_path.stem}_impedance.png"
            if target_img and Path(target_img).exists():
                if sys.platform == "darwin":
                    subprocess.Popen(["open", str(target_img)])
                elif sys.platform.startswith("win"):
                    os.startfile(str(target_img))
                else:
                    subprocess.Popen(["xdg-open", str(target_img)])
            else:
                self.generate_and_show_topology()

    def start_simulation(self):
        if not self.binary_path or not self.binary_path.exists():
            QMessageBox.critical(
                self,
                "Executable C++ Tidak Ditemukan",
                f"Executable C++20 Woodbury Engine tidak ditemukan.\n"
                f"Lokasi pencarian terakhir: {self.binary_path}\n\n"
                f"Pastikan berkas contingency_n2_woodbury.exe (Windows) atau contingency_n2_woodbury (macOS/Linux) "
                f"telah terpasang dengan benar."
            )
            return
        if not self.current_case_path or not self.current_case_path.exists():
            QMessageBox.critical(
                self,
                "Berkas Kasus Grid Tidak Ditemukan",
                f"Berkas jaringan MATPOWER tidak ditemukan di:\n{self.current_case_path}\n\n"
                f"Silakan gunakan tombol '📁 Pilih Kasus Grid (.m)' untuk memilih berkas sistem transmisi."
            )
            return

        self.btn_run.setEnabled(False)
        self.btn_stop.setEnabled(True)
        self.progress_bar.setRange(0, 0)
        self.lbl_status.setText(f"Sedang mengevaluasi kontingensi pada {self.current_case_path.name}...")
        self.txt_log.clear()

        threads = self.slider_threads.value()
        limit = self.spin_limit.value()
        force_all = self.chk_all.isChecked()

        self.worker = SimulationWorker(
            self.binary_path, self.current_case_path, threads, limit, force_all
        )
        self.worker.log_signal.connect(self.append_log)
        self.worker.finished_signal.connect(self.on_simulation_finished)
        self.worker.start()

    def append_log(self, text):
        self.txt_log.append(text.rstrip())

    def stop_simulation(self):
        if self.worker:
            self.worker.stop()
            self.lbl_status.setText("Simulasi dihentikan pengguna.")
            self.btn_run.setEnabled(True)
            self.btn_stop.setEnabled(False)
            self.progress_bar.setRange(0, 100)
            self.progress_bar.setValue(0)

    def on_simulation_finished(self, success, message, data):
        self.btn_run.setEnabled(True)
        self.btn_stop.setEnabled(False)
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(100)

        if not success:
            QMessageBox.warning(self, "Simulasi Gagal", message)
            self.lbl_status.setText("Simulasi gagal.")
            return

        self.current_results = data
        self.lbl_status.setText("✓ Skrining Kontingensi Selesai.")
        self.btn_export_excel.setEnabled(True)
        self.btn_export_json.setEnabled(True)

        # Update Grid Info
        self.lbl_grid_buses.setText(f"• Jumlah Bus      : {data.get('num_buses', '-')}")
        self.lbl_grid_branches.setText(f"• Jumlah Saluran  : {data.get('num_branches', '-')}")
        self.lbl_grid_load.setText(f"• Beban Total     : {data.get('total_load_mw', 0):,.2f} MW")
        self.lbl_grid_losses.setText(f"• Rugi Base-Case  : {data.get('base_ploss_mw', 0):,.2f} MW")

        # Update KPI Cards
        perf = data.get("performance", {})
        solves_sec = perf.get("throughput_solves_per_sec", 0)
        wall_ms = perf.get("total_wall_time_ms", 0)
        avg_us = perf.get("avg_solve_time_us", 0)

        self.lbl_val_solves.setText(f"{solves_sec:,.0f}")
        self.lbl_val_time.setText(f"{wall_ms:,.1f}")
        self.lbl_val_avg.setText(f"{avg_us:,.1f}")

        # Populate Table
        self.populate_table(data.get("top_critical_contingencies", []))
        self.tabs.setCurrentIndex(0)

    def populate_table(self, contingencies):
        self.table.setRowCount(0)
        self.table.setRowCount(len(contingencies))

        for row, c in enumerate(contingencies):
            pair_str = f"#{c.get('branch1_idx', 0)+1} & #{c.get('branch2_idx', 0)+1}"
            f1 = f"{c.get('from_bus1')} ➔ {c.get('to_bus1')}"
            f2 = f"{c.get('from_bus2')} ➔ {c.get('to_bus2')}"
            status = c.get("status", "UNKNOWN")
            max_load = c.get("max_loading_pct", 0.0)
            overloads = c.get("overloaded_branch_count", 0)
            vmin = c.get("min_voltage", 1.0)
            vmin_bus = c.get("min_voltage_bus", 0)
            pi_tot = c.get("PI_Total", 0.0)

            item_rank = QTableWidgetItem(str(c.get("rank", row + 1)))
            item_pair = QTableWidgetItem(pair_str)
            item_f1 = QTableWidgetItem(f1)
            item_f2 = QTableWidgetItem(f2)
            item_status = QTableWidgetItem(status)
            item_load = QTableWidgetItem(f"{max_load:.1f}%" if status == "CONVERGED" else "-")
            item_over = QTableWidgetItem(str(overloads))
            item_vmin = QTableWidgetItem(f"{vmin:.3f} (B#{vmin_bus})" if status == "CONVERGED" else "-")
            item_pi = QTableWidgetItem(f"{pi_tot:,.1f}")

            if status == "DIVERGED":
                item_status.setBackground(QColor("#7f1d1d"))
                item_status.setForeground(QColor("#fecaca"))
                item_status.setText("⚪ DIVERGED (KOLAPS)")
            elif status == "ISLANDING":
                item_status.setBackground(QColor("#78350f"))
                item_status.setForeground(QColor("#fde68a"))
                item_status.setText(f"🟡 ISLAND ({c.get('isolated_load_mw', 0):.0f}MW)")
            elif max_load > 100.0:
                item_status.setBackground(QColor("#7c2d12"))
                item_status.setForeground(QColor("#fed7aa"))
                item_status.setText("🔴 OVERLOAD")
                item_load.setForeground(QColor("#f87171"))
                item_load.setFont(QFont("Segoe UI", 11, QFont.Bold))
            else:
                item_status.setBackground(QColor("#064e3b"))
                item_status.setForeground(QColor("#a7f3d0"))
                item_status.setText("🟢 STABIL")

            if status == "CONVERGED" and vmin < 0.90:
                item_vmin.setForeground(QColor("#fbbf24"))
                item_vmin.setFont(QFont("Segoe UI", 11, QFont.Bold))

            items = [
                item_rank, item_pair, item_f1, item_f2,
                item_status, item_load, item_over, item_vmin, item_pi
            ]

            for col_idx, item in enumerate(items):
                if col_idx in [0, 1, 4, 5, 6, 8]:
                    item.setTextAlignment(Qt.AlignCenter)
                self.table.setItem(row, col_idx, item)

        if len(contingencies) > 0:
            self.table.selectRow(0)
            self.on_table_row_selected()

    def on_table_row_selected(self):
        selected_rows = self.table.selectionModel().selectedRows()
        if not selected_rows or not self.current_results:
            return

        row = selected_rows[0].row()
        conts = self.current_results.get("top_critical_contingencies", [])
        if row < len(conts):
            c = conts[row]
            b1 = c.get('branch1_idx', 0) + 1
            b2 = c.get('branch2_idx', 0) + 1
            pair_name = f"Trip Saluran #{b1} ({c.get('from_bus1')}➔{c.get('to_bus1')}) & #{b2} ({c.get('from_bus2')}➔{c.get('to_bus2')})"
            self.lbl_detail_title.setText(
                f"Detail Inspeksi & Aliran Daya: Rank #{c.get('rank')} — {pair_name}"
            )
            detail_text = f"""================================================================================
                    LAPORAN ANALISIS KONTINGENSI GANDA (N-2)
================================================================================
1. SALURAN YANG MENGALAMI GANGGUAN / TRIP:
   - Saluran 1 : Bus {c.get('from_bus1')} ➔ Bus {c.get('to_bus1')} (Index #{b1})
   - Saluran 2 : Bus {c.get('from_bus2')} ➔ Bus {c.get('to_bus2')} (Index #{b2})

2. DAMPAK KEANDALAN TERHADAP SISTEM TRANSMISI:
   - Status Sistem              : {c.get('status')}
   - Indeks Keparahan Komposit   : {c.get('PI_Total'):,.2f}
   - Indeks Penalti Tegangan     : {c.get('PI_V'):,.2f}
   - Indeks Penalti Termal (PI_P): {c.get('PI_P'):,.2f}

3. PROFIL RINGKAS PEMBEBANAN & TEGANGAN:
   - Pembebanan Saluran Tertinggi: {c.get('max_loading_pct'):,.2f}%
   - Jumlah Saluran Overload     : {c.get('overloaded_branch_count')} saluran
   - Tegangan Terendah           : {c.get('min_voltage'):.4f} p.u. (Terjadi pada Bus #{c.get('min_voltage_bus')})
   - Pelepasan Beban (Islanding) : {c.get('isolated_load_mw'):,.2f} MW
   - Total Rugi Transmisi Kasus  : {c.get('Ploss_mw'):,.2f} MW

4. REKOMENDASI OPERASI (DISPATCHER GUIDELINE):
   {'⚠️ PERINGATAN: Potensi pemadaman subsistem / islanding. Laporan aliran daya tiap island disajikan lengkap di bawah.' if c.get('status') == 'ISLANDING' else ''}
   {'🔴 KRITIS: Terjadi beban lebih berat. Segera lakukan re-dispatch pembangkit lokal atau manuver jaringan.' if c.get('max_loading_pct') > 100.0 else ''}
   {'🟢 NORMAL: Gangguan N-2 ini mampu ditoleransi oleh sistem tanpa melanggar batas operasional standar.' if c.get('status') == 'CONVERGED' and c.get('max_loading_pct') <= 100.0 else ''}
"""
            # Hitung Studi Aliran Daya Lengkap (AC Newton-Raphson / Multi-Island)
            try:
                if str(self.project_root) not in sys.path:
                    sys.path.insert(0, str(self.project_root))
                from scripts.island_power_flow import analyze_island_contingency, format_island_report
                isl_res = analyze_island_contingency(str(self.current_case_path), [b1, b2])
                pf_report_str = format_island_report(isl_res, include_tables=True)
                detail_text += f"\n\n{pf_report_str}"
            except Exception as ex:
                detail_text += f"\n\n[Gagal menghitung aliran daya lengkap: {ex}]"

            self.txt_detail.setText(detail_text.strip())

    def copy_detail_report(self):
        text = self.txt_detail.toPlainText()
        if text:
            clipboard = QApplication.clipboard()
            clipboard.setText(text)
            QMessageBox.information(self, "Berhasil", "Laporan detail aliran daya telah disalin ke clipboard.")

    def save_detail_report(self):
        text = self.txt_detail.toPlainText()
        if not text:
            return
        fpath, _ = QFileDialog.getSaveFileName(self, "Simpan Laporan Aliran Daya", "Laporan_Aliran_Daya_N2.txt", "Text Files (*.txt)")
        if fpath:
            try:
                with open(fpath, "w", encoding="utf-8") as f:
                    f.write(text)
                QMessageBox.information(self, "Berhasil", f"Laporan berhasil disimpan di:\n{fpath}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Gagal menyimpan file: {e}")

    def filter_table(self):
        search_text = self.txt_search.text().strip().lower()
        status_filter = self.cmb_status.currentText()

        for row in range(self.table.rowCount()):
            pair = self.table.item(row, 1).text().lower()
            f1 = self.table.item(row, 2).text().lower()
            f2 = self.table.item(row, 3).text().lower()
            status_text = self.table.item(row, 4).text()

            match_search = (search_text in pair) or (search_text in f1) or (search_text in f2)
            match_status = True

            if "Kritis / Overload" in status_filter:
                match_status = ("OVERLOAD" in status_text)
            elif "Islanding" in status_filter:
                match_status = ("ISLAND" in status_text)
            elif "Divergen" in status_filter:
                match_status = ("DIVERGED" in status_text)

            self.table.setRowHidden(row, not (match_search and match_status))

    def export_excel(self):
        if not self.current_results:
            return
        fpath, _ = QFileDialog.getSaveFileName(self, "Simpan Laporan Excel", "Laporan_Kontingensi_N2.xlsx", "Excel (*.xlsx)")
        if not fpath:
            return

        try:
            import pandas as pd
            conts = self.current_results.get("top_critical_contingencies", [])
            df = pd.DataFrame(conts)
            df.to_excel(fpath, index=False)
            QMessageBox.information(self, "Berhasil", f"Laporan Excel berhasil disimpan di:\n{fpath}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Gagal mengekspor Excel: {e}")

    def export_json_action(self):
        if not self.current_results:
            return
        fpath, _ = QFileDialog.getSaveFileName(self, "Simpan JSON", "Laporan_Kontingensi_N2.json", "JSON (*.json)")
        if not fpath:
            return

        try:
            with open(fpath, "w") as f:
                json.dump(self.current_results, f, indent=2)
            QMessageBox.information(self, "Berhasil", f"Berkas JSON berhasil disimpan di:\n{fpath}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Gagal menyimpan JSON: {e}")


def main():
    app = QApplication(sys.argv)
    window = GridGuardMainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
