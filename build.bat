@echo off
setlocal
title Build Lite PDF Presentation (C++ Native)

echo =======================================================
echo     BUILD LITE PDF PRESENTATION (C++ NATIVE)
echo =======================================================
echo.

where g++.exe >nul 2>&1
if errorlevel 1 (
    if exist "%USERPROFILE%\mingw64\bin\g++.exe" (
        set "PATH=%USERPROFILE%\mingw64\bin;%PATH%"
    ) else (
        if exist "C:\mingw64\bin\g++.exe" (
            set "PATH=C:\mingw64\bin;%PATH%"
        ) else (
            echo [ERROR] g++ compiler not found in PATH or standard locations!
            pause
            exit /b 1
        )
    )
)

echo [1/3] Compiling application icon resource...
windres.exe resource.rc -O coff -o resource.res
if errorlevel 1 (
    echo [ERROR] Failed to compile resource.rc!
    pause
    exit /b 1
)

echo.
echo [2/3] Compiling C++ code with g++ (Optimization -O3)...
g++.exe -O3 -municode -mwindows -static main.cpp resource.res -o "Lite PDF Presentation.exe" -Iinclude -Llib -l:pdfium.dll.lib -lcomctl32 -lcomdlg32 -lshell32 -lgdi32
if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo.
echo [3/3] Checking pdfium.dll runtime...
if not exist "pdfium.dll" (
    if exist "bin\pdfium.dll" (
        copy /y "bin\pdfium.dll" "pdfium.dll" >nul
    )
)

if not exist "dist" mkdir "dist"
copy /y "Lite PDF Presentation.exe" "dist\Lite PDF Presentation.exe" >nul
copy /y "pdfium.dll" "dist\pdfium.dll" >nul
if exist "sample_presentation.pdf" copy /y "sample_presentation.pdf" "dist\sample_presentation.pdf" >nul

echo.
echo =======================================================
echo                  BUILD SUCCESSFUL!
echo =======================================================
echo Standalone executable created:
echo  - %CD%\Lite PDF Presentation.exe
echo Distribution folder:
echo  - %CD%\dist\
echo =======================================================
echo.
pause
