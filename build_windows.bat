@echo off
REM ==============================================================================
REM   GRIDGUARD N-2 — WINDOWS ONE-CLICK BUILD & PACKAGING SCRIPT
REM ==============================================================================
setlocal enabledelayedexpansion

echo [1/4] Memeriksa Lingkungan Python dan Visual Studio / MSVC...
where python >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Python tidak terdeteksi. Silakan pasang Python 3.10+ untuk Windows.
    pause
    exit /b 1
)

echo [2/4] Memasang dependensi Python...
python -m pip install --upgrade pip
pip install PySide6 networkx scipy matplotlib numpy pyinstaller pillow

echo [3/4] Mengompilasi C++20 Woodbury Engine...
if not exist "build" mkdir build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target contingency_n2_woodbury

echo [4/4] Memaketkan Aplikasi GridGuard dengan PyInstaller...
pyinstaller --noconfirm --clean GridGuard_win.spec

echo.
echo ==============================================================================
echo Selesai! Aplikasi portable siap di folder: dist\GridGuard\GridGuard.exe
echo.
echo Untuk membuat installer Setup.exe:
echo Jalankan Inno Setup Compiler pada berkas 'installer_windows.iss'.
echo ==============================================================================
pause
