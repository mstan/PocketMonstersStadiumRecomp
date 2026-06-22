# Cart-validation flake trial harness.
# One trial: kill any instance, launch with PSR_GBSELFTEST, wait for boot,
# press start to reveal the cart-select slots, foreground + screenshot, and
# save the per-trial stderr log (the [gbself] self-test/SRAM trace). Leaves
# the runner killed at the end so trials are independent.
#
# Usage: cv_trial.ps1 -Index <n> [-Port 4372]
param(
  [int]$Index = 0,
  [int]$Port = 4372
)
$ErrorActionPreference = "SilentlyContinue"
$proj = "F:\Projects\n64recomp\PocketMonstersStadiumRecomp"
$outdir = "$proj\build\cv_trials"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null
$shot = "$outdir\cv_$($Index.ToString('00')).png"
$elog = "$outdir\err_$($Index.ToString('00')).log"

Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

$env:PMS_VOLUME="1.0"; $env:PMS_DEBUG_PORT="$Port"; $env:PSR_INTERP_DISCOVERY="1"; $env:PSR_GBSELFTEST="1"
$rom = "$proj\baserom.z64"
$p = Start-Process "$proj\build\PocketMonstersStadiumRecomp.exe" -ArgumentList "`"$rom`"" -WorkingDirectory "$proj\build" -RedirectStandardError $elog -PassThru

# Wait for boot (autoplay countdown + game init + cart validation).
python "$proj\tools\pms_drive.py" --port $Port --pid $($p.Id) boot:1300 | Out-Null
# Reveal the cart-select slots.
python "$proj\tools\pms_drive.py" --port $Port --pid $($p.Id) press:start wait:1.0 | Out-Null

# Foreground for a real (non-black) capture.
Add-Type @"
using System; using System.Runtime.InteropServices;
public class FgT {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
}
"@
$pp = Get-Process -Id $($p.Id) -EA SilentlyContinue
if ($pp -and $pp.MainWindowHandle -ne 0) {
  [FgT]::ShowWindow($pp.MainWindowHandle,9) | Out-Null
  [FgT]::SetForegroundWindow($pp.MainWindowHandle) | Out-Null
}
Start-Sleep -Milliseconds 500
& powershell -ExecutionPolicy Bypass -File "$proj\tools\pms_screenshot.ps1" $shot -ProcessId $($p.Id) | Out-Null

# Summarize the [gbself] STATUS reads (cart=1 reads that are NOT 0x89 = status anomaly).
$g = Get-Content $elog -EA SilentlyContinue | Select-String '\[gbself\] RD STATUS'
$cart1 = $g | Where-Object { $_ -match 'cart=1' }
$bad = $cart1 | Where-Object { $_ -notmatch 'val=0x89' }
$sram = (Get-Content $elog -EA SilentlyContinue | Select-String '\[gbself\] RD SRAM ram_en=0' | Measure-Object).Count
"trial $Index : shot=$shot cart1_status=$(($cart1|Measure-Object).Count) anomalous_status=$(($bad|Measure-Object).Count) sram_ramdis_reads=$sram"
if (($bad | Measure-Object).Count -gt 0) { "  ANOMALOUS STATUS LINES:"; $bad | ForEach-Object { "    " + $_.Line } }

Get-Process PocketMonstersStadiumRecomp,PokemonStadiumRecomp -EA SilentlyContinue | Stop-Process -Force
