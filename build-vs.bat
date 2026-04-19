@echo off
set CMAKE="D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo === Generating VS2026 project ===
%CMAKE% --preset vs2026
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    pause
    exit /b 1
)

echo === Building Debug ===
%CMAKE% --build --preset vs2026-debug
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo === Done ===
pause
