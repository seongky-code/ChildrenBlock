#Requires -RunAsAdministrator
<#
.SYNOPSIS
    ChildrenBlock 제거 (관리자 PowerShell 에서 실행)

.PARAMETER KeepData
    지정하면 %ProgramData%\ChildrenBlock (설정, 사용 기록) 을 남긴다.
#>
param([switch]$KeepData)

$ErrorActionPreference = 'Stop'

Get-Process ChildrenBlock -ErrorAction SilentlyContinue | Stop-Process -Force
Get-ScheduledTask -TaskName 'ChildrenBlock*' -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$false

$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\ChildrenBlock 설정.lnk'
if (Test-Path $shortcut) { Remove-Item $shortcut -Force }

$installDir = Join-Path $env:ProgramFiles 'ChildrenBlock'
if (Test-Path $installDir) { Remove-Item $installDir -Recurse -Force }

if (-not $KeepData) {
    $dataDir = Join-Path $env:ProgramData 'ChildrenBlock'
    if (Test-Path $dataDir) { Remove-Item $dataDir -Recurse -Force }
}

Write-Host '제거 완료.' -ForegroundColor Green
