# Boot-logo softlock repro harness.
# Mirrors the sequence that hung once: launch, foreground + drive input during
# the LOGO window (SI/controller traffic on t6's path during cart validation),
# then screenshot + dump ALL always-on rings to per-index files. The depth-64
# never-evict per-queue table retains the full boot-init handshake even on a
# late dump, so we capture everything every iteration and identify the hung run
# afterward from the screenshots.
#
# Usage: cv_repro.ps1 -Index <n> [-Port 4372]
param([int]$Index = 0, [int]$Port = 4372)
$ErrorActionPreference = "SilentlyContinue"
$proj = "F:\Projects\n64recomp\PocketMonstersStadiumRecomp"
$outdir = "$proj\build\cv_repro"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null
$ix = $Index.ToString('00')

Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$env:PMS_VOLUME="0.0"; $env:PMS_DEBUG_PORT="$Port"; $env:PSR_INTERP_DISCOVERY="1"; $env:PSR_GBSELFTEST="1"
$rom = "$proj\baserom.z64"
$p = Start-Process "$proj\build\PocketMonstersStadiumRecomp.exe" -ArgumentList "`"$rom`"" -WorkingDirectory "$proj\build" -RedirectStandardError "$outdir\err_$ix.log" -PassThru
$pid0 = $p.Id

Add-Type @"
using System; using System.Runtime.InteropServices;
public class FgR { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n); }
"@
function Foreground($id) { $pp = Get-Process -Id $id -EA SilentlyContinue; if ($pp -and $pp.MainWindowHandle -ne 0) { [FgR]::ShowWindow($pp.MainWindowHandle,9)|Out-Null; [FgR]::SetForegroundWindow($pp.MainWindowHandle)|Out-Null } }

# Drive input DURING the logo window (early), like the run that hung.
python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 boot:1000 | Out-Null
Foreground $pid0
python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 press:start wait:2 press:start wait:3 press:start wait:2 | Out-Null

# Screenshot (foreground first for a real capture).
Foreground $pid0
Start-Sleep -Milliseconds 400
& powershell -ExecutionPolicy Bypass -File "$proj\tools\pms_screenshot.ps1" "$outdir\shot_$ix.png" -ProcessId $pid0 | Out-Null

# Dump all always-on rings (never-evict tables persist even on a late dump).
python "$proj\tools\pms_drive.py" --port $Port --pid $pid0 dump_sched "dump_mesg:0" gbcart_dump:6000 status | Out-Null
Start-Sleep -Milliseconds 300
Copy-Item "$proj\build\pms_sched_dump.txt"        "$outdir\sched_$ix.txt"  -Force -EA SilentlyContinue
Copy-Item "$proj\build\build\pms_sched_dump.txt"  "$outdir\sched_$ix.txt"  -Force -EA SilentlyContinue
Copy-Item "$proj\build\pms_mesg_dump.txt"         "$outdir\mesg_$ix.txt"   -Force -EA SilentlyContinue
Copy-Item "$proj\build\build\pms_mesg_dump.txt"   "$outdir\mesg_$ix.txt"   -Force -EA SilentlyContinue
Copy-Item "$proj\build\build\gbcart_ring.txt"     "$outdir\gbcart_$ix.txt" -Force -EA SilentlyContinue

# Classify: hung if the boot-init pipeline threads (t5/t6/t7/t10) are all parked
# with no recent activity. Read the never-evict thread table.
$sched = Get-Content "$outdir\sched_$ix.txt" -EA SilentlyContinue
$gb = (Get-Content "$outdir\gbcart_$ix.txt" -EA SilentlyContinue | Select-String 'total=(\d+)').Matches.Groups[1].Value
$t6line = ($sched | Select-String 'NEVER-EVICT' -Context 0,9 | Select-Object -First 1)
# crude: report t6/t7 'ago' from the never-evict block
$nv = @()
$inblock = $false
foreach ($ln in $sched) {
  if ($ln -match 'per-thread state \(NEVER-EVICT') { $inblock = $true; continue }
  if ($inblock) { if ($ln -match '^\s*t(\d+)\s.*?(\d+)ms_ago\s+events=(\d+)\s+blocked_on_recv_q=(0x[0-9A-Fa-f]+)') { $nv += [pscustomobject]@{ t=$matches[1]; ago=[int]$matches[2]; ev=$matches[3]; q=$matches[4] } } elseif ($ln -match '^---') { break } }
}
$t6 = $nv | Where-Object { $_.t -eq '6' } | Select-Object -First 1
$t7 = $nv | Where-Object { $_.t -eq '7' } | Select-Object -First 1
"trial $Index : gbcart_ops=$gb  t6(ago=$($t6.ago) q=$($t6.q))  t7(ago=$($t7.ago) q=$($t7.q))  shot=$outdir\shot_$ix.png"

Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
