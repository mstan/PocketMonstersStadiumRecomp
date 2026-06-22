# Fast ring-based cart-validation flake trial.
# Per trial: kill, launch, boot, enter cart-select, dump the gbcart ring to a
# per-trial file, run analyze_gbcart_ring.py, and report the two bad-run
# detectors: open-bus SRAM reads (>1 = beyond the benign pre-enable read) and
# delivery-vs-Rev1 mismatches (>1). No screenshot/foreground — the ring is the
# signal, so trials run unattended and don't steal window focus.
#
# Usage: cv_ringtrial.ps1 -Index <n> [-Port 4372]
param([int]$Index = 0, [int]$Port = 4372)
$ErrorActionPreference = "SilentlyContinue"
$proj = "F:\Projects\n64recomp\PocketMonstersStadiumRecomp"
$outdir = "$proj\build\cv_trials"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null
$ring = "$outdir\ring_$($Index.ToString('00')).txt"
$elog = "$outdir\err_$($Index.ToString('00')).log"
$liveRing = "$proj\build\build\gbcart_ring.txt"  # server writes here (cwd=build)

Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$env:PMS_VOLUME="0.0"; $env:PMS_DEBUG_PORT="$Port"; $env:PSR_INTERP_DISCOVERY="1"; $env:PSR_GBSELFTEST="1"
$rom = "$proj\baserom.z64"
$p = Start-Process "$proj\build\PocketMonstersStadiumRecomp.exe" -ArgumentList "`"$rom`"" -WorkingDirectory "$proj\build" -RedirectStandardError $elog -PassThru

# Boot + reach cart-select (validation runs by the time the slots are drawn).
python "$proj\tools\pms_drive.py" --port $Port --pid $($p.Id) boot:1200 wait:2 press:start wait:1.5 press:start wait:1.0 | Out-Null
# Dump the full validation ring.
python "$proj\tools\pms_drive.py" --port $Port --pid $($p.Id) gbcart_dump:5000 | Out-Null
Start-Sleep -Milliseconds 300
if (Test-Path $liveRing) { Copy-Item $liveRing $ring -Force }

Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force

# Analyze: extract the two detector lines + thread line.
if (Test-Path $ring) {
  $a = python "$proj\tools\analyze_gbcart_ring.py" $ring 2>&1
  $rows   = ($a | Select-String '^parsed rows:').Line
  $thr    = ($a | Select-String '^threads issuing').Line
  $flake  = ($a | Select-String 'FLAKE SIGNATURE\] SRAM').Line
  $rev1   = ($a | Select-String '\[delivery vs Rev1\] samples').Line
  $multi  = ($a | Select-String 'MULTIPLE THREADS').Line
  "trial $Index : $rows | $thr"
  "          $flake"
  "          $rev1"
  if ($multi) { "          ** $multi **" }
  # Flag a likely bad run: open-bus reads >1 OR rev1 mismatches >1.
  $fn = 0; if ($flake -match ':\s*(\d+)') { $fn = [int]$matches[1] }
  $rm = 99; if ($rev1 -match 'mismatches=(\d+)') { $rm = [int]$matches[1] }
  if ($fn -gt 1 -or $rm -gt 1) { "          >>> BAD-RUN CANDIDATE (open_bus=$fn rev1_mismatch=$rm) ring=$ring" }
} else {
  "trial $Index : NO RING CAPTURED (boot/drive failed)"
}
