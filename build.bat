@echo off
setlocal
title Build Lite PDF Presentation

echo =======================================================
echo          BUILD LITE PDF PRESENTATION TO .EXE
echo =======================================================
echo.

python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Python not found on system!
    pause
    exit /b 1
)

if not exist ".venv\Scripts\python.exe" (
    echo [1/3] Creating isolated virtual environment...
    python -m venv .venv
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to create virtual environment!
        pause
        exit /b 1
    )
)

echo [1/3] Installing / Checking dependencies...
call .venv\Scripts\pip.exe install -r requirements.txt
if %errorlevel% neq 0 (
    echo [ERROR] Failed to install dependencies!
    pause
    exit /b 1
)

echo.
echo [2/3] Cleaning previous build files...
if exist build rmdir /s /q build
if exist "dist\Lite PDF Presentation.exe" del /f /q "dist\Lite PDF Presentation.exe"

echo.
echo [3/3] Compiling application with PyInstaller...
call .venv\Scripts\pyinstaller.exe --noconsole --onefile --clean --icon="app_icon.ico" --add-data "app_icon.ico;." --name "Lite PDF Presentation" main.py

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo =======================================================
echo                  BUILD SUCCESSFUL!
echo =======================================================
echo Output executable location:
echo %CD%\dist\Lite PDF Presentation.exe
echo =======================================================
echo.
pause
