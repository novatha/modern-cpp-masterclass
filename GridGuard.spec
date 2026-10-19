# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path
import os
import sys

block_cipher = None
project_root = Path('/Users/novaliodaratha/Documents/2026/mengajar/cpp').resolve()

datas = [
    (str(project_root / 'data'), 'data'),
    (str(project_root / 'assets'), 'assets'),
    (str(project_root / 'scripts'), 'scripts'),
]

binaries = [
    (str(project_root / 'build' / 'contingency_n2_woodbury'), 'build'),
]

# Tambahkan dylib SuiteSparse & OpenMP jika ada
ss_lib = Path('/opt/homebrew/opt/suite-sparse/lib')
omp_lib = Path('/opt/homebrew/opt/libomp/lib')

if ss_lib.exists():
    for f in ss_lib.glob('*.dylib'):
        if f.is_file() and not f.is_symlink():
            binaries.append((str(f), '.'))
if omp_lib.exists():
    for f in omp_lib.glob('*.dylib'):
        if f.is_file() and not f.is_symlink():
            binaries.append((str(f), '.'))

hiddenimports = [
    'PySide6.QtWebEngineWidgets',
    'PySide6.QtWebEngineCore',
    'PySide6.QtPrintSupport',
    'PySide6.QtGui',
    'PySide6.QtCore',
    'PySide6.QtWidgets',
    'scripts',
    'scripts.island_power_flow',
    'scripts.plot_network_topology',
    'networkx',
    'scipy',
    'scipy.sparse',
    'scipy.sparse.csgraph',
    'matplotlib',
    'matplotlib.pyplot',
    'matplotlib.lines',
    'numpy',
]

a = Analysis(
    ['gui/gridguard_app.py'],
    pathex=[str(project_root)],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['tkinter', 'PyQt5', 'PyQt6', 'wx', 'IPython'],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='GridGuard',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch='arm64',
    codesign_identity=None,
    entitlements_file=None,
    icon=str(project_root / 'assets' / 'AppIcon.icns'),
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='GridGuard',
)

app = BUNDLE(
    coll,
    name='GridGuard.app',
    icon=str(project_root / 'assets' / 'AppIcon.icns'),
    bundle_identifier='id.pln.gridguard',
    info_plist={
        'CFBundleName': 'GridGuard',
        'CFBundleDisplayName': 'GridGuard N-2',
        'CFBundleIdentifier': 'id.pln.gridguard',
        'CFBundleVersion': '2.1.0',
        'CFBundleShortVersionString': '2.1.0',
        'NSHumanReadableCopyright': 'Copyright © 2026 PT PLN (Persero) / ITB & UNIB Power Systems Research. All rights reserved.',
        'NSHighResolutionCapable': 'True',
        'NSRequiresAquaSystemAppearance': 'False',
        'LSMinimumSystemVersion': '12.0',
    },
)
