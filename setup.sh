#!/usr/bin/env bash
# PocketMonstersStadiumRecomp setup - POSIX
#
# Provisions lib/N64ModernRuntime and lib/rt64 as symlinks to the sister
# checkouts (../N64ModernRuntime, ../rt64) if present, else clones them
# from mstan/* at the branch in n64recomp.pin; then initializes
# N64ModernRuntime's N64Recomp submodule (the engine).
#
# lib/ is .gitignored; the SHAs each fork is known to boot against are
# recorded in n64recomp.pin.
set -euo pipefail

N64MR_REPO="https://github.com/mstan/N64ModernRuntime.git"
RT64_REPO="https://github.com/mstan/rt64.git"
BRANCH="work/pocket-monsters-stadium"
SISTER_N64MR="../N64ModernRuntime"
SISTER_RT64="../rt64"

mkdir -p lib

# ---- lib/N64ModernRuntime ----
if [ ! -e "lib/N64ModernRuntime" ]; then
    if [ -d "$SISTER_N64MR/.git" ]; then
        echo "Symlinking lib/N64ModernRuntime -> $SISTER_N64MR"
        ln -s "$(cd "$SISTER_N64MR" && pwd)" "lib/N64ModernRuntime"
    else
        echo "Cloning N64ModernRuntime..."
        git clone --branch "$BRANCH" --recurse-submodules "$N64MR_REPO" "lib/N64ModernRuntime"
    fi
fi

# ---- lib/rt64 ----
if [ ! -e "lib/rt64" ]; then
    if [ -d "$SISTER_RT64/.git" ]; then
        echo "Symlinking lib/rt64 -> $SISTER_RT64"
        ln -s "$(cd "$SISTER_RT64" && pwd)" "lib/rt64"
    else
        echo "Cloning rt64..."
        git clone --branch "$BRANCH" "$RT64_REPO" "lib/rt64"
    fi
fi

# ---- N64Recomp submodule (inside N64ModernRuntime) ----
git -C "lib/N64ModernRuntime" submodule update --init --recursive

# ---- ROM reminder ----
if [ ! -f "baserom.z64" ]; then
    echo
    echo "NOTE: baserom.z64 not found. Place your verified Pocket Monsters"
    echo "      Stadium (J) ROM at the repo root as baserom.z64 before"
    echo "      running the recompiler / launching the build."
fi

echo
echo "Setup complete."
echo "Next:"
echo "  1. cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"
echo "  2. cmake --build build"
echo "  3. See ghidra/ and tools/ghidra_seed.py for analysis setup."
