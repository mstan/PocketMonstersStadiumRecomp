@echo off
:: PocketMonstersStadiumRecomp setup - Windows
::
:: What this does:
::   1. Provisions lib/N64ModernRuntime and lib/rt64 as junctions to the
::      sister checkouts (..\N64ModernRuntime, ..\rt64) if present, else
::      clones them from mstan/* at the branch in n64recomp.pin.
::   2. Junctions the RmlUi-launcher UI libs (RmlUi/lunasvg/freetype/...) from
::      ..\Zelda64Recomp\lib (version-matched to the fork lineage).
::   3. Initializes N64ModernRuntime's N64Recomp submodule (the engine).
::   4. Reminds you to drop the verified PMS-J ROM in as baserom.z64.
::
:: lib/ is .gitignored; the SHAs each fork is known to boot against are
:: recorded in n64recomp.pin.

setlocal enabledelayedexpansion

git submodule update --init --recursive lib/N64ModernRuntime lib/rt64
if errorlevel 1 exit /b %errorlevel%

set "N64MR_REPO=https://github.com/mstan/N64ModernRuntime.git"
set "RT64_REPO=https://github.com/mstan/rt64.git"
set "BRANCH=main"
set "SISTER_N64MR=..\N64ModernRuntime"
set "SISTER_RT64=..\rt64"
:: UI libs (RmlUi launcher) are version-matched to the rt64/N64MR fork lineage
:: (Zelda64Recomp@ab677e7) and live inside Zelda64Recomp/lib; junction them in.
set "SISTER_ZELDA_LIB=..\Zelda64Recomp\lib"

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

:: ---- Provision UI libs for the RmlUi launcher (junction to Zelda64Recomp/lib) ----
:: RmlUi/lunasvg/freetype/GamepadMotionHelpers/SlotMap/concurrentqueue are not
:: standalone forks; they ship inside Zelda64Recomp/lib. nfd comes from rt64's
:: contrib. FindFreetype.cmake is tracked under cmake/ (lib/ is gitignored).
for %%n in (RmlUi lunasvg freetype-windows-binaries GamepadMotionHelpers SlotMap concurrentqueue) do (
    if not exist "lib\%%n" (
        if exist "%SISTER_ZELDA_LIB%\%%n" (
            echo Junctioning lib\%%n -^> %SISTER_ZELDA_LIB%\%%n
            mklink /J "lib\%%n" "%SISTER_ZELDA_LIB%\%%n" >nul
        ) else (
            echo WARNING: %SISTER_ZELDA_LIB%\%%n not found.
            echo          Clone Zelda64Recomp next to this repo for the launcher build.
        )
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
