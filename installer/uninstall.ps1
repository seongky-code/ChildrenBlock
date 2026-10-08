#Requires -RunAsAdministrator
<#
.SYNOPSIS
    ChildrenBlock 제거 (관리자 PowerShell 에서 실행)

.PARAMETER KeepData
    지정하면 %ProgramData%\ChildrenBlock (설정, 사용 기록) 을 남긴다.
#>
param([switch]$KeepData)

$ErrorActionPreference = 'Stop'
$serviceName = 'ChildrenBlock'

if (Get-Service $serviceName -ErrorAction SilentlyContinue) {
    Stop-Service $serviceName -Force -ErrorAction SilentlyContinue  # 서비스가 트레이 앱도 함께 종료
    sc.exe delete $serviceName | Out-Null
}
Get-ScheduledTask -TaskName 'ChildrenBlock*' -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$false
Get-Process ChildrenBlock, ChildrenBlockService -ErrorAction SilentlyContinue | Stop-Process -Force

$startMenu = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'
foreach ($name in 'ChildrenBlock 설정.lnk', 'ChildrenBlock 사용 기록.lnk') {
    $path = Join-Path $startMenu $name
    if (Test-Path $path) { Remove-Item $path -Force }
}

$installDir = Join-Path $env:ProgramFiles 'ChildrenBlock'
if (Test-Path $installDir) {
    Start-Sleep 1
    Remove-Item $installDir -Recurse -Force
}

if (-not $KeepData) {
    $dataDir = Join-Path $env:ProgramData 'ChildrenBlock'
    if (Test-Path $dataDir) { Remove-Item $dataDir -Recurse -Force }
}

Write-Host '제거 완료.' -ForegroundColor Green
