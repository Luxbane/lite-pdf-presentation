@echo off
title Lite PDF Presentation
echo Launching Lite PDF Presentation...
python main.py
if %errorlevel% neq 0 (
    echo.
    echo An error occurred while running the application.
    pause
)
