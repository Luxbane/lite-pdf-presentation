@echo off
title Lite PDF Presentation
echo Launching Lite PDF Presentation...
if exist ".venv\Scripts\python.exe" (
    call .venv\Scripts\python.exe main.py
) else (
    python main.py
)
if %errorlevel% neq 0 (
    echo.
    echo An error occurred while running the application.
    pause
)
