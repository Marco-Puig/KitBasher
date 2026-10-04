@echo off
setlocal

rem Run this from the project root (same folder as CMakeLists.txt).
rem Optional first argument: build configuration, e.g. "build.bat Release".
cd /d "%~dp0"

set CONFIG=Debug
if not "%~1"=="" set CONFIG=%~1

echo Configuring...
cmake -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.11"
if errorlevel 1 (
    echo Configure failed.
    pause
    exit /b 1
)

echo Building %CONFIG%...
cmake --build build --config %CONFIG%
if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

echo Build completed successfully.
echo Output: build\bin\%CONFIG%\KitBasher.exe
