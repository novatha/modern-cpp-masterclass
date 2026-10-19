#!/usr/bin/env python3
"""
GridGuard N-2 — macOS Standalone Application & DMG Installer Builder
Memaketkan GridGuard menjadi native macOS App Bundle (GridGuard.app)
dan berkas installer Disk Image (.dmg) dengan drag-and-drop ke /Applications.
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent

def run_cmd(cmd, desc):
    print(f"\n▶ {desc}...")
    print(f"  $ {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    res = subprocess.run(cmd, cwd=PROJECT_ROOT, shell=isinstance(cmd, str))
    if res.returncode != 0:
        print(f"❌ Error: {desc} gagal dengan exit code {res.returncode}.")
        sys.exit(res.returncode)
    print(f"✓ {desc} berhasil!")

def main():
    print("================================================================================")
    print("     GRIDGUARD N-2 — MACOS APP & DMG INSTALLER BUILD PIPELINE                   ")
    print("================================================================================")

    # 1. Pastikan C++ binary tersedia
    cpp_bin = PROJECT_ROOT / "build" / "contingency_n2_woodbury"
    if not cpp_bin.exists():
        print("⚙ Mengompilasi engine C++20 Woodbury...")
        run_cmd(["cmake", "--build", "build", "--target", "contingency_n2_woodbury", "-j8"], "Kompilasi C++20 Engine")

    if not cpp_bin.exists():
        print(f"❌ Binary {cpp_bin} tidak ditemukan!")
        sys.exit(1)

    # 2. Pastikan AppIcon.icns tersedia
    icon_icns = PROJECT_ROOT / "assets" / "AppIcon.icns"
    if not icon_icns.exists():
        print("⚙ Menghasilkan AppIcon.icns...")
        run_cmd([sys.executable, "scripts/generate_app_icon.py"], "Generate App Icon")

    # 3. Jalankan PyInstaller
    pyinstaller_bin = PROJECT_ROOT / ".venv" / "bin" / "pyinstaller"
    if not pyinstaller_bin.exists():
        pyinstaller_bin = Path(shutil.which("pyinstaller") or "pyinstaller")

    print(f"Menggunakan PyInstaller: {pyinstaller_bin}")
    run_cmd([str(pyinstaller_bin), "--noconfirm", "--clean", "GridGuard.spec"], "Build GridGuard.app dengan PyInstaller")

    app_bundle = PROJECT_ROOT / "dist" / "GridGuard.app"
    if not app_bundle.exists():
        print(f"❌ Bundle {app_bundle} tidak ditemukan!")
        sys.exit(1)

    print(f"\n✓ macOS Application Bundle siap di: {app_bundle}")

    # 4. Pastikan izin eksekusi (chmod +x) untuk semua binary di dalam bundle
    macos_dir = app_bundle / "Contents" / "MacOS"
    for item in macos_dir.rglob("*"):
        if item.is_file() and not item.is_symlink():
            if item.name in ["GridGuard", "contingency_n2_woodbury"] or item.suffix in ["", ".dylib"]:
                try:
                    os.chmod(item, 0o755)
                except Exception:
                    pass

    # 5. Ad-hoc Codesigning (Wajib untuk macOS Apple Silicon & Gatekeeper)
    print("\n▶ Melakukan Ad-Hoc Codesigning...")
    sign_cmd = ["codesign", "--force", "--deep", "--sign", "-", str(app_bundle)]
    run_cmd(sign_cmd, "Codesigning GridGuard.app")

    # 6. Pembuatan DMG Installer (Drag-and-Drop)
    dmg_staging = PROJECT_ROOT / "dist" / "dmg_staging"
    if dmg_staging.exists():
        shutil.rmtree(dmg_staging)
    dmg_staging.mkdir(parents=True, exist_ok=True)

    print(f"Menyiapkan folder staging DMG: {dmg_staging}")
    # Salin GridGuard.app ke staging
    run_cmd(["cp", "-R", str(app_bundle), str(dmg_staging / "GridGuard.app")], "Salin GridGuard.app ke staging")

    # Buat symlink Applications
    app_link = dmg_staging / "Applications"
    if not app_link.exists():
        os.symlink("/Applications", app_link)

    # Tambahkan Volume Icon jika ada
    if icon_icns.exists():
        shutil.copy2(icon_icns, dmg_staging / ".VolumeIcon.icns")

    # Output DMG path
    dmg_path = PROJECT_ROOT / "dist" / "GridGuard-N2-Installer.dmg"
    if dmg_path.exists():
        dmg_path.unlink()

    dmg_cmd = [
        "hdiutil", "create",
        "-volname", "GridGuard N-2 Installer",
        "-srcfolder", str(dmg_staging),
        "-ov",
        "-format", "UDZO",
        str(dmg_path)
    ]
    run_cmd(dmg_cmd, "Membuat Disk Image (.dmg) Installer")

    # Bersihkan staging
    if dmg_staging.exists():
        shutil.rmtree(dmg_staging)

    dmg_size_mb = dmg_path.stat().st_size / (1024 * 1024)

    print("\n" + "=" * 80)
    print("🎉 SUKSES! APLIKASI DAN INSTALLER MACOS SIAP DIGUNAKAN:")
    print("=" * 80)
    print(f"1. Standalone macOS App : {app_bundle}")
    print(f"2. Installer DMG         : {dmg_path} ({dmg_size_mb:.1f} MB)")
    print("\nCara Menginstall:")
    print("  • Buka file DMG: double click GridGuard-N2-Installer.dmg (atau jalankan: open dist/GridGuard-N2-Installer.dmg)")
    print("  • Tarik ikon 'GridGuard.app' ke folder 'Applications'.")
    print("  • Buka GridGuard dari Launchpad / Spotlight / folder Applications!")
    print("================================================================================\n")

if __name__ == "__main__":
    main()
