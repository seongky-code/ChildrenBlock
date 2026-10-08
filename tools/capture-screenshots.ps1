<#
.SYNOPSIS
    README 용 스크린샷 생성 (docs\images\*.png)

.DESCRIPTION
    Debug 빌드의 서비스를 콘솔 모드(--console, 테스트 모드)로 띄우고 예시 데이터로 화면을 캡처한다.
    실제 설치·설정과 분리되도록 임시 데이터 폴더와 별도 파이프 이름을 쓴다 (Debug 빌드 전용 기능).
    프로그램 창만 캡처하므로 바탕화면이나 다른 창은 찍히지 않는다.
    휴식 화면 캡처 중 몇 초간 전체 화면이 덮인다.

    powershell -ExecutionPolicy Bypass -File tools\capture-screenshots.ps1
#>
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $env:TEMP 'ChildrenBlockDocs'
New-Item -ItemType Directory -Force $root | Out-Null
$bin = "$repo\bin\x64\Debug"
$out = "$repo\docs\images"
New-Item -ItemType Directory -Force $out | Out-Null
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -Namespace Shot -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, string l);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
'@
[Shot.W]::SetProcessDPIAware() | Out-Null

function Wait-Window([string]$title, [int]$timeoutSec = 10) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $h = [Shot.W]::FindWindow([NullString]::Value, $title)
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 200
    }
    throw "window not found: $title"
}

function Save-Window([IntPtr]$h, [string]$name) {
    Start-Sleep -Milliseconds 600
    $r = New-Object Shot.W+RECT
    [Shot.W]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null  # DWMWA_EXTENDED_FRAME_BOUNDS
    $w = $r.R - $r.L; $hgt = $r.B - $r.T
    $bmp = New-Object Drawing.Bitmap $w, $hgt
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object Drawing.Size $w, $hgt))
    $bmp.Save("$out\$name.png", [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    "saved $name.png (${w}x$hgt)"
}

function Save-Screen([string]$name, [int]$width = 1280) {
    Start-Sleep -Milliseconds 800
    $b = [Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object Drawing.Bitmap $b.Width, $b.Height
    $g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location, [Drawing.Point]::Empty, $b.Size)
    $small = New-Object Drawing.Bitmap $width, ([int]($b.Height * $width / $b.Width))
    $g2 = [Drawing.Graphics]::FromImage($small); $g2.InterpolationMode = 'HighQualityBicubic'
    $g2.DrawImage($bmp, 0, 0, $small.Width, $small.Height)
    $small.Save("$out\$name.png", [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $g2.Dispose(); $bmp.Dispose(); $small.Dispose()
    "saved $name.png"
}

# --- sample data ---
$data = Join-Path $root 'cbdocs'
if (Test-Path $data) { Remove-Item -Recurse -Force $data }
New-Item -ItemType Directory $data | Out-Null
$salt = New-Object byte[] 16; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($salt)
$hash = (New-Object Security.Cryptography.Rfc2898DeriveBytes('1234', $salt, 200000, [Security.Cryptography.HashAlgorithmName]::SHA256)).GetBytes(32)
$hex = { param($b) ($b | ForEach-Object { $_.ToString('x2') }) -join '' }
$settings = @"
[Schedule]
BreaksEnabled=1
UseMinutes=30
BreakMinutes=5
BreakWarnSeconds=60
DailyLimitEnabled=1
DailyLimitMinutes=120
ShutdownAtEnabled=1
ShutdownAtMinute=1260
ShutdownWarnSeconds=300
CountBreakAsUsage=0
[Children]
Users=minjun,seoyeon
[Security]
Salt=$(& $hex $salt)
Hash=$(& $hex $hash)
Iterations=200000
"@
$settings | Set-Content -Encoding ascii "$data\settings.ini"

$today = Get-Date
$minutes = 95, 110, 62, 135, 88, 0, 120, 74, 101, 128, 55, 92, 115, 140, 83, 66, 97, 125, 70, 65
$lines = @('[State]', "Date=$($today.ToString('yyyyMMdd'))", 'UsedTodayMs=3900000', '[History]')
for ($i = 19; $i -ge 0; $i--) {
    $m = $minutes[19 - $i]; $bonus = if ($m -gt 120) { 1800000 } else { 0 }
    $lines += "$($today.AddDays(-$i).ToString('yyyyMMdd'))=$($m * 60000),$bonus"
}
$lines | Set-Content -Encoding ascii "$data\state-minjun.ini"

$env:CHILDRENBLOCK_DATADIR = $data
$env:CHILDRENBLOCK_PIPE = 'ChildrenBlockDocs'
function Start-Svc([string[]]$extra = @()) {
    $p = Start-Process "$bin\ChildrenBlockService.exe" -ArgumentList (@('--console') + $extra) -PassThru -WindowStyle Hidden
    Start-Sleep 2; return $p
}
function Stop-Svc($p) {
    [Threading.EventWaitHandle]::OpenExisting('Local\ChildrenBlock.ConsoleStop').Set() | Out-Null
    $p.WaitForExit(15000) | Out-Null; Start-Sleep 1
}

# --- dialogs (no tracking: service without --child-all) ---
$svc = Start-Svc
try {
    $p = Start-Process "$bin\ChildrenBlock.exe" -ArgumentList '/history' -PassThru
    Save-Window (Wait-Window 'ChildrenBlock - 사용 기록') 'history'
    Stop-Process -Id $p.Id -Force

    $p = Start-Process "$bin\ChildrenBlock.exe" -ArgumentList '/setup' -PassThru
    $h = Wait-Window 'ChildrenBlock - 비밀번호'
    [Shot.W]::SendMessage([Shot.W]::GetDlgItem($h, 1002), 0x000C, [IntPtr]::Zero, '1234') | Out-Null  # WM_SETTEXT → IDC_PASSWORD
    [Shot.W]::PostMessage($h, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null  # WM_COMMAND IDOK
    Save-Window (Wait-Window 'ChildrenBlock - 설정') 'settings'
    Stop-Process -Id $p.Id -Force
}
finally { Stop-Svc $svc }

# --- tray-driven screens: track the current session, but the state shown has no names ---
$me = $env:USERNAME.ToLower()
function Shot-State([string]$state, [scriptblock]$capture) {
    "[State]`r`nDate=$($today.ToString('yyyyMMdd'))`r`n$state" | Set-Content -Encoding ascii "$data\state-$me.ini"
    $svc = Start-Svc @('--child-all')
    try { Start-Sleep 2; & $capture } finally { Stop-Svc $svc }
}

Shot-State "UsedTodayMs=3000000`r`nCycleUsedMs=$(30 * 60000 - 45000)`r`nBreakRemainingMs=0`r`nBonusMs=0" {
    Save-Window (Wait-Window 'ChildrenBlock 알림') 'break-warning'
}
Shot-State "UsedTodayMs=$(120 * 60000 - 222000)`r`nCycleUsedMs=0`r`nBreakRemainingMs=0`r`nBonusMs=0" {
    Save-Window (Wait-Window 'ChildrenBlock 알림') 'shutdown-warning'
}
Shot-State "UsedTodayMs=3000000`r`nCycleUsedMs=1800000`r`nBreakRemainingMs=192000`r`nBonusMs=0" {
    Wait-Window 'ChildrenBlock 휴식' | Out-Null
    Save-Screen 'break-screen'
}

# --- icon ---
& "$repo\tools\make-icons.ps1" -OutDir (Join-Path $root 'iconsdocs') -Preview | Out-Null
Copy-Item (Join-Path $root 'iconsdocs\ChildrenBlock-256.png') "$out\icon.png" -Force
"saved icon.png"

"leftover processes: " + @(Get-Process ChildrenBlock, ChildrenBlockService -ErrorAction SilentlyContinue).Count
