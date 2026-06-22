# Large-tail boot-logo softlock repro: loop until a hang is caught, then export
# a BIG slice of the 65536-event mesg ring (tail=60000) so the raw stream
# reaches back past the moment t3 stopped completing the gfx task (the DONE-to-
# pipeline stop, ~7-8s before detection on a typical catch). The depth-64
# per-queue table is flooded out by VI retrace on the shared interrupt queue
# 0x800AA100, so the raw ring is the only place the low-frequency SP/DP-done
# completion that triggers DONE survives — and only if exported in bulk.
#
# Usage: cv_repro_big.ps1 [-Max 40] [-Port 4372] [-HangMs 4000]
param([int]$Max = 40, [int]$Port = 4372, [int]$HangMs = 4000)
$ErrorActionPreference = "SilentlyContinue"
$proj = "F:\Projects\n64recomp\PocketMonstersStadiumRecomp"
$outdir = "$proj\build\cv_repro"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null
$rom = "$proj\baserom.z64"
$env:PMS_VOLUME="0.0"; $env:PMS_DEBUG_PORT="$Port"; $env:PSR_INTERP_DISCOVERY="1"; $env:PSR_GBSELFTEST="1"

for ($i = 1; $i -le $Max; $i++) {
  Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 400
  $p = Start-Process "$proj\build\PocketMonstersStadiumRecomp.exe" -ArgumentList "`"$rom`"" -WorkingDirectory "$proj\build" -RedirectStandardError "$outdir\big_err.log" -PassThru
  $pid0 = $p.Id
  python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 boot:1000 press:start wait:2 press:start wait:3 press:start wait:2 | Out-Null
  python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 dump_sched | Out-Null
  Start-Sleep -Milliseconds 200
  $sched = Get-Content "$proj\build\pms_sched_dump.txt" -EA SilentlyContinue
  if (-not $sched) { $sched = Get-Content "$proj\build\build\pms_sched_dump.txt" -EA SilentlyContinue }
  $t6ago = -1; $inblock = $false
  foreach ($ln in $sched) {
    if ($ln -match 'per-thread state \(NEVER-EVICT') { $inblock = $true; continue }
    if ($inblock) { if ($ln -match '^\s*t6\s.*?(\d+)ms_ago') { $t6ago = [int]$matches[1]; break }; if ($ln -match '^--- per-thread LAST') { break } }
  }
  "iter $i : t6_ago=$t6ago hung=$($t6ago -ge $HangMs)"
  if ($t6ago -ge $HangMs) {
    # BIG raw dump: export ~the whole ring to reach the DONE-stop transition.
    python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 "dump_mesg:0:60000" gbcart_dump:6000 | Out-Null
    Start-Sleep -Milliseconds 400
    Copy-Item "$proj\build\pms_sched_dump.txt"       "$outdir\HUNGBIG_sched.txt"  -Force -EA SilentlyContinue
    Copy-Item "$proj\build\build\pms_sched_dump.txt" "$outdir\HUNGBIG_sched.txt"  -Force -EA SilentlyContinue
    Copy-Item "$proj\build\pms_mesg_dump.txt"        "$outdir\HUNGBIG_mesg.txt"   -Force -EA SilentlyContinue
    Copy-Item "$proj\build\build\pms_mesg_dump.txt"  "$outdir\HUNGBIG_mesg.txt"   -Force -EA SilentlyContinue
    Copy-Item "$proj\build\build\gbcart_ring.txt"    "$outdir\HUNGBIG_gbcart.txt" -Force -EA SilentlyContinue
    "   >>> HUNG (big dump) CAPTURED -> HUNGBIG_*  (t6 parked $t6ago ms); mesg lines:"
    (Get-Content "$outdir\HUNGBIG_mesg.txt" | Measure-Object -Line).Lines
    Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
    return
  }
  Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
}
"no hang caught in $Max iters"
