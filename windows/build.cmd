@echo off
REM Build wrapper. Sets up MSVC env, configures with CMake+Ninja, builds.
REM   build.cmd           -> dev build (RelWithDebInfo) in .\build       (fast iteration)
REM   build.cmd release   -> optimized Release (/O2) in .\build-release,
REM                          then copies the single portable exe to .\dist\MindfulCompute.exe
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "NINJA=C:\Users\Muchen\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe"
cd /d "%~dp0"

if /I "%~1"=="release" (
    set "BUILD_DIR=build-release"
    set "BUILD_TYPE=Release"
) else (
    set "BUILD_DIR=build"
    set "BUILD_TYPE=RelWithDebInfo"
)

REM Re-configuring every build refreshes the timestamped build stamp (see CMakeLists).
"%CMAKE%" -S . -B %BUILD_DIR% -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_CXX_COMPILER=cl || exit /b 1
"%CMAKE%" --build %BUILD_DIR% || exit /b 1

if /I "%~1"=="release" (
    if not exist dist mkdir dist
    copy /y "%BUILD_DIR%\MindfulCompute.exe" "dist\MindfulCompute.exe" >nul || exit /b 1
    echo DIST: dist\MindfulCompute.exe
    for %%F in ("dist\MindfulCompute.exe") do echo SIZE: %%~zF bytes
    REM Ship the .pdb next to the exe in dist so crash dumps from the laptop can
    REM be symbolized here. The pdb is NOT the exe -- copying it does not change
    REM the portable single-file exe; the user only takes the exe to the laptop.
    if exist "%BUILD_DIR%\MindfulCompute.pdb" (
        copy /y "%BUILD_DIR%\MindfulCompute.pdb" "dist\MindfulCompute.pdb" >nul
        echo PDB: dist\MindfulCompute.pdb
    )
)
echo BUILD_OK
