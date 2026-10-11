@echo off
setlocal
cd /d "%~dp0"
title CCS engine - Build

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install Visual Studio Build Tools with "Desktop development with C++"
  echo and CMake, then retry.
  pause
  exit /b 1
)
if not exist build mkdir build
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
if errorlevel 1 goto fail
cmake --build build --config Release
if errorlevel 1 goto fail

if exist "Publish\CCS engine.exe" (
  echo.
  echo SUCCESS: %CD%\Publish\CCS engine.exe
  echo.
  pause
  exit /b 0
)
echo Build completed, but Publish\CCS engine.exe was not found.
goto fail

:fail
echo.
echo Build failed. Review the error messages above.
pause
exit /b 1

