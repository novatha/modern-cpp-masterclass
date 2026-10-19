# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path
import os
import sys

block_cipher = None
project_root = Path('.').resolve()

datas = [
    (str(project_root / 'data'), 'data'),
    (str(project_root / 'assets'), 'assets'),
    (str(project_root / 'scripts'), 'scripts'),
]

binaries = []
# On Windows, contingency_n2_woodbury.exe is located in build or build/Release
for cand in [
    project_root / 'build' / 'Release' / 'contingency_n2_woodbury.exe',
    project_root / 'build' / 'contingency_n2_woodbury.exe',
    project_root / 'contingency_n2_woodbury.exe'
]:
    if cand.exists():
        binaries.append((str(cand), 'build'))
        break

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
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=str(project_root / 'assets' / 'AppIcon.ico'),
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
