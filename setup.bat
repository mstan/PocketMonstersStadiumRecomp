@echo off
:: PocketMonstersStadiumRecomp setup - Windows
::
:: What this does:
::   1. Provisions lib/N64ModernRuntime and lib/rt64 as junctions to the
::      sister checkouts (..\N64ModernRuntime, ..\rt64) if present, else
::      clones them from mstan/* at the branch in n64recomp.pin.
::   2. Initializes N64ModernRuntime's N64Recomp submodule (the engine).
::   3. Reminds you to drop the verified PMS-J ROM in as baserom.z64.
::
:: lib/ is .gitignored; the SHAs each fork is known to boot against are
:: recorded in n64recomp.pin.

setlocal enabledelayedexpansion

set "N64MR_REPO=https://github.com/mstan/N64ModernRuntime.git"
set "RT64_REPO=https://github.com/mstan/rt64.git"
set "BRANCH=main"
set "SISTER_N64MR=..\N64ModernRuntime"
set "SISTER_RT64=..\rt64"

if not exist "lib" mkdir "lib"

:: ---- Provision lib\N64ModernRuntime ----
if not exist "lib\N64ModernRuntime" (
    if exist "%SISTER_N64MR%\.git" (
        echo Junctioning lib\N64ModernRuntime -^> %SISTER_N64MR%
        mklink /J "lib\N64ModernRuntime" "%SISTER_N64MR%"
    ) else (
        echo Cloning N64ModernRuntime...
        git clone --branch %BRANCH% --recurse-submodules %N64MR_REPO% "lib\N64ModernRuntime"
    )
)

:: ---- Provision lib\rt64 ----
if not exist "lib\rt64" (
    if exist "%SISTER_RT64%\.git" (
        echo Junctioning lib\rt64 -^> %SISTER_RT64%
        mklink /J "lib\rt64" "%SISTER_RT64%"
    ) else (
        echo Cloning rt64...
        git clone --branch %BRANCH% %RT64_REPO% "lib\rt64"
    )
)

:: ---- N64Recomp submodule (lives inside N64ModernRuntime) ----
git -C "lib\N64ModernRuntime" submodule update --init --recursive

:: ---- ROM reminder ----
if not exist "baserom.z64" (
    echo.
    echo NOTE: baserom.z64 not found. Place your verified Pocket Monsters
    echo       Stadium ^(J^) ROM at the repo root as baserom.z64 before
    echo       running the recompiler / launching the build.
)

echo.
echo Setup complete.
echo Next:
echo   1. cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
echo   2. cmake --build build
echo   3. See ghidra\ and tools\ghidra_seed.py for analysis setup.

endlocal
