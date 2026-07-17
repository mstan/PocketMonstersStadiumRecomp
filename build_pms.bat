@echo off
:: PMS build helper — vcvars64 + cmake(Ninja, clang-cl) configure + build.
:: Logs to _cfg_pms.log (configure) and _build_pms.log (build).
:: Portable: project dir = this script's location, VS located via vswhere.
setlocal

set "PROJ=%~dp0"
if "%PROJ:~-1%"=="\" set "PROJ=%PROJ:~0,-1%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo vswhere not found - install Visual Studio 2022 with C++ tools & exit /b 1 )

set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" ( echo No VS install with C++ tools found & exit /b 1 )

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" ( echo vcvars64.bat not found at "%VCVARS%" & exit /b 1 )

:: Prefer the CMake bundled with Visual Studio (a PATH cmake from MSYS2/devkitPro
:: can shadow it and break the Ninja + clang-cl configuration).
set "CMAKE=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"

call "%VCVARS%" >nul
if errorlevel 1 ( echo VCVARS FAILED & exit /b 1 )

:: clang-cl is required (the generated C relies on clang warning flags cl.exe
:: rejects). vcvars64 doesn't put LLVM on PATH, so prepend it and select clang-cl.
set "PATH=%VSINSTALL%\VC\Tools\Llvm\x64\bin;%PATH%"
if not exist "%VSINSTALL%\VC\Tools\Llvm\x64\bin\clang-cl.exe" (
    echo clang-cl not found - install the "C++ Clang tools for Windows" VS component
    exit /b 1
)

echo === CONFIGURE ===
"%CMAKE%" -S "%PROJ%" -B "%PROJ%\build" -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release > "%PROJ%\_cfg_pms.log" 2>&1
set CFG_RC=%errorlevel%
echo configure rc=%CFG_RC%
if not "%CFG_RC%"=="0" ( echo CONFIGURE FAILED rc=%CFG_RC% & exit /b %CFG_RC% )

:: Throttle so the machine stays usable: cap parallel jobs (default 6, override
:: PMS_BUILD_JOBS) AND run below-normal priority (clang-cl children inherit it).
:: Build ALL targets (no --target) so the game exe AND the standalone
:: recomp-ui launcher (pmsj-launcher) both build.
if not defined PMS_BUILD_JOBS set "PMS_BUILD_JOBS=6"
echo === BUILD (throttled: -j %PMS_BUILD_JOBS%, IDLE priority) ===
:: /low = IDLE priority class (lowest) so foreground apps/games are never
:: starved; the build only runs on otherwise-idle CPU. clang-cl children
:: inherit the class. Longer wall-clock is the intended trade.
start "" /low /b /wait "%CMAKE%" --build "%PROJ%\build" -- -j %PMS_BUILD_JOBS% > "%PROJ%\_build_pms.log" 2>&1
set BLD_RC=%errorlevel%
echo build rc=%BLD_RC%
exit /b %BLD_RC%
