@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion

echo ============================================
echo   File Unlocker - Build Script
echo ============================================
echo.

REM Check g++
where g++ >nul 2>&1
if %errorlevel% neq 0 goto try_msvc

echo [MinGW] Found g++, compiling...
echo.

windres resource.rc -O coff -o resource.res >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARN] windres failed, building without manifest...
    g++ -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE -o FileUnlocker.exe main.cpp -lrstrtmgr -lpsapi -lcomctl32 -lcomdlg32 -lshell32
) else (
    g++ -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE -o FileUnlocker.exe main.cpp resource.res -lrstrtmgr -lpsapi -lcomctl32 -lcomdlg32 -lshell32
)

if %errorlevel% == 0 (
    echo.
    echo [OK] Build successful: FileUnlocker.exe
) else (
    echo.
    echo [FAIL] g++ compilation failed.
)
goto done

:try_msvc
where cl >nul 2>&1
if %errorlevel% neq 0 goto no_compiler

echo [MSVC] Found cl.exe, compiling...
rc /fo resource.res resource.rc >nul 2>&1
cl /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE /Fe:FileUnlocker.exe main.cpp resource.res /link /SUBSYSTEM:WINDOWS rstrtmgr.lib psapi.lib comctl32.lib comdlg32.lib shell32.lib user32.lib gdi32.lib

if %errorlevel% == 0 (
    echo [OK] Build successful: FileUnlocker.exe
) else (
    echo [FAIL] MSVC compilation failed.
)
goto done

:no_compiler
echo [ERROR] No compiler found (g++ or cl.exe).
echo.
echo Please install one of:
echo   1. MSYS2 (recommended): https://www.msys2.org
echo      Then run: pacman -S mingw-w64-ucrt-x86_64-gcc
echo   2. Visual Studio Build Tools: https://visualstudio.microsoft.com/downloads/
echo.

:done
endlocal
pause
