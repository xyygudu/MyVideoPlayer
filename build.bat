@echo off
if not exist "%~dp0build" (
    cmake --preset default
)
cmake --build build %*
