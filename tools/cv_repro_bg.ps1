# Background boot-logo softlock repro loop (no foreground/screenshot, so it can
# run unattended without stealing window focus). Loops N iterations; each:
# launch, drive input during the logo window, dump the always-on rings, and
# classify by the TRUE hang signature = t6 (the Transfer Pak / validation
# thread) parked with no recent activity. A healthy boot keeps t6 active
# (ago <= a few hundred ms); the hang parks it for tens of seconds. On a HUNG
# detection, the dumps are preserved with a HUNG_<n>_ prefix (depth-64
# never-evict tables retain the full handshake).
#
# Usage: cv_repro_bg.ps1 [-Count 20] [-Port 4372] [-HangMs 4000]
param([int]$Count = 20, [int]$Port = 4372, [int]$HangMs = 4000)
$ErrorActionPreference = "SilentlyContinue"
$proj = "F:\Projects\n64recomp\PocketMonstersStadiumRecomp"
$outdir = "$proj\build\cv_repro"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null
$rom = "$proj\baserom.z64"
$env:PMS_VOLUME="0.0"; $env:PMS_DEBUG_PORT="$Port"; $env:PSR_INTERP_DISCOVERY="1"; $env:PSR_GBSELFTEST="1"
$summary = "$outdir\bg_summary.txt"
"=== bg repro run, Count=$Count HangMs=$HangMs ===" | Out-File $summary

for ($i = 1; $i -le $Count; $i++) {
  Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 400
  $p = Start-Process "$proj\build\PocketMonstersStadiumRecomp.exe" -ArgumentList "`"$rom`"" -WorkingDirectory "$proj\build" -RedirectStandardError "$outdir\bg_err.log" -PassThru
  $pid0 = $p.Id

  # Boot + drive input during the logo window (the trigger correlate).
  python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 boot:1000 press:start wait:2 press:start wait:3 press:start wait:2 | Out-Null
  # Dump rings (never-evict tables survive a late dump).
  python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 dump_sched "dump_mesg:0" gbcart_dump:6000 status | Out-Null
  Start-Sleep -Milliseconds 250

  $sched = Get-Content "$proj\build\pms_sched_dump.txt" -EA SilentlyContinue
  if (-not $sched) { $sched = Get-Content "$proj\build\build\pms_sched_dump.txt" -EA SilentlyContinue }
  # Parse t6 'ago' from the never-evict thread table.
  $t6ago = -1; $inblock = $false
  foreach ($ln in $sched) {
    if ($ln -match 'per-thread state \(NEVER-EVICT') { $inblock = $true; continue }
    if ($inblock) {
      if ($ln -match '^\s*t6\s.*?(\d+)ms_ago') { $t6ago = [int]$matches[1]; break }
      if ($ln -match '^--- per-thread LAST') { break }
    }
  }
  $gb = (Get-Content "$proj\build\build\gbcart_ring.txt" -EA SilentlyContinue | Select-String 'total=(\d+)').Matches.Groups[1].Value
  $hung = ($t6ago -ge $HangMs)
  $line = "iter $i : t6_ago=$t6ago gbcart_ops=$gb hung=$hung"
  $line | Tee-Object -Append $summary

  if ($hung) {
    $ix = $i.ToString('00')
    Copy-Item "$proj\build\pms_sched_dump.txt"       "$outdir\HUNG_${ix}_sched.txt"  -Force -EA SilentlyContinue
    Copy-Item "$proj\build\build\pms_sched_dump.txt" "$outdir\HUNG_${ix}_sched.txt"  -Force -EA SilentlyContinue
    Copy-Item "$proj\build\pms_mesg_dump.txt"        "$outdir\HUNG_${ix}_mesg.txt"   -Force -EA SilentlyContinue
    Copy-Item "$proj\build\build\pms_mesg_dump.txt"  "$outdir\HUNG_${ix}_mesg.txt"   -Force -EA SilentlyContinue
    Copy-Item "$proj\build\build\gbcart_ring.txt"    "$outdir\HUNG_${ix}_gbcart.txt" -Force -EA SilentlyContinue
    Copy-Item "$outdir\bg_err.log"                   "$outdir\HUNG_${ix}_err.log"    -Force -EA SilentlyContinue
    "   >>> HUNG CAPTURED -> HUNG_${ix}_*  (t6 parked $t6ago ms)" | Tee-Object -Append $summary
  }
}
Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
"=== bg repro done ===" | Tee-Object -Append $summary
