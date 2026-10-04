@echo off
setlocal

rem Run this from the project root (same folder as CMakeLists.txt).
rem Optional first argument: build config, e.g. "run.bat Release" (default: Debug)
cd /d "%~dp0"
set "ROOT=%~dp0"

set CONFIG=Debug
if not "%~1"=="" set CONFIG=%~1

echo Building %CONFIG% ...
cmake --build build --config %CONFIG%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

set EXE=

if exist "build\bin\%CONFIG%\KitBasher.exe" set EXE=build\bin\%CONFIG%\KitBasher.exe
if "%EXE%"=="" if exist "build\%CONFIG%\KitBasher.exe" set EXE=build\%CONFIG%\KitBasher.exe
if "%EXE%"=="" if exist "build\bin\KitBasher.exe" set EXE=build\bin\KitBasher.exe
if "%EXE%"=="" if exist "build\KitBasher.exe" set EXE=build\KitBasher.exe

if "%EXE%"=="" (
    echo Could not find KitBasher.exe under build\. Build it first with rebuild.bat.
    exit /b 1
)

echo Running %EXE% ...
rem Run from the exe's own folder so relative paths (e.g. resources\) resolve correctly.
set "EXE_DIR=%ROOT%build\bin\%CONFIG%"
pushd "%EXE_DIR%"
if errorlevel 1 (
    echo Could not enter the executable directory.
    pause
    exit /b 1
)
.\KitBasher.exe
set RUN_CODE=%ERRORLEVEL%
popd
if not "%RUN_CODE%"=="0" (
    echo KitBasher exited with code %RUN_CODE%.
    pause
)
exit /b %RUN_CODE%