#Requires -RunAsAdministrator
<#
.SYNOPSIS
    ChildrenBlock 설치 (관리자 PowerShell 에서 실행)

.DESCRIPTION
    1. 실행 파일을 Program Files\ChildrenBlock 에 복사
    2. 부모 비밀번호 등록 및 초기 설정 (ChildrenBlock.exe /setup)
    3. %ProgramData%\ChildrenBlock 권한 설정
       - settings.ini : 일반 사용자 읽기 전용 (자녀가 수정/삭제 불가)
       - state.ini, log.txt : 일반 사용자 쓰기 가능, 삭제 불가
    4. 자녀 계정 로그온 시 자동 실행되도록 작업 스케줄러 등록
    5. 시작 메뉴에 'ChildrenBlock 설정' 바로 가기 생성

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -ChildUser 민준
#>
param(
    # 제한을 적용할 자녀의 Windows 계정 이름 (여러 명이면 쉼표로 구분)
    [Parameter(Mandatory = $true)]
    [string[]]$ChildUser,

    [string]$Source = (Join-Path $PSScriptRoot '..\bin\x64\Release\ChildrenBlock.exe')
)

$ErrorActionPreference = 'Stop'

$installDir = Join-Path $env:ProgramFiles 'ChildrenBlock'
$exe = Join-Path $installDir 'ChildrenBlock.exe'
$dataDir = Join-Path $env:ProgramData 'ChildrenBlock'
$taskPrefix = 'ChildrenBlock'

if (-not (Test-Path $Source)) {
    throw "실행 파일이 없습니다: $Source`n먼저 Release 로 빌드하세요."
}

foreach ($user in $ChildUser) {
    if (-not (Get-LocalUser -Name $user -ErrorAction SilentlyContinue)) {
        throw "로컬 계정 '$user' 을(를) 찾을 수 없습니다."
    }
    try {
        # 연결이 끊긴 SID 가 그룹에 있으면 Get-LocalGroupMember 가 실패하므로 경고 확인만 건너뛴다.
        if ((Get-LocalGroupMember -SID 'S-1-5-32-544' | Where-Object { $_.Name -like "*\$user" })) {
            Write-Warning "'$user' 는 관리자 계정입니다. 관리자 계정은 제한을 우회할 수 있으므로 표준 사용자로 바꾸는 것을 권장합니다."
        }
    } catch {
        Write-Warning "관리자 그룹을 확인하지 못했습니다. '$user' 가 표준 사용자인지 직접 확인하세요."
    }
}

# 1. 실행 중인 인스턴스 종료 후 복사
Get-Process ChildrenBlock -ErrorAction SilentlyContinue | Stop-Process -Force
New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item $Source $exe -Force
Write-Host "설치: $exe"

# 2. 데이터 폴더 + 부모 비밀번호/설정
New-Item -ItemType Directory -Force $dataDir | Out-Null
Write-Host '부모 비밀번호와 초기 설정을 입력하세요...'
Start-Process $exe -ArgumentList '/setup' -Wait
$settings = Join-Path $dataDir 'settings.ini'
if (-not (Select-String -Path $settings -Pattern '^Hash=.+' -Quiet -ErrorAction SilentlyContinue)) {
    throw '비밀번호가 등록되지 않았습니다. 설치를 다시 실행하세요.'
}

# 3. 권한 설정
foreach ($name in 'state.ini', 'log.txt') {
    $path = Join-Path $dataDir $name
    if (-not (Test-Path $path)) { New-Item -ItemType File $path | Out-Null }
}

$system = New-Object Security.Principal.SecurityIdentifier 'S-1-5-18'
$admins = New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-544'
$users = New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-545'
$inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
$none = [Security.AccessControl.InheritanceFlags]::None
$prop = [Security.AccessControl.PropagationFlags]::None
$allow = [Security.AccessControl.AccessControlType]::Allow

$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true, $false)  # 상속 끊기
$acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($system, 'FullControl', $inherit, $prop, $allow)))
$acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($admins, 'FullControl', $inherit, $prop, $allow)))
$acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($users, 'ReadAndExecute', $inherit, $prop, $allow)))
Set-Acl $dataDir $acl

foreach ($name in 'state.ini', 'log.txt') {
    $path = Join-Path $dataDir $name
    $fileAcl = Get-Acl $path
    $fileAcl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($users, 'Read, Write', $none, $prop, $allow)))
    Set-Acl $path $fileAcl
}
Write-Host "권한 설정: $dataDir"

# 4. 자녀 계정 로그온 시 자동 실행
Get-ScheduledTask -TaskName "$taskPrefix*" -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$false
foreach ($user in $ChildUser) {
    $account = "$env:COMPUTERNAME\$user"
    $action = New-ScheduledTaskAction -Execute $exe
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $account
    $principal = New-ScheduledTaskPrincipal -UserId $account -LogonType Interactive -RunLevel Limited
    $taskSettings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName "$taskPrefix - $user" -Action $action -Trigger $trigger `
        -Principal $principal -Settings $taskSettings -Force | Out-Null
    Write-Host "자동 실행 등록: $account"
}

# 5. 시작 메뉴 바로 가기 (부모가 설정 변경할 때 사용)
$shortcutPath = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\ChildrenBlock 설정.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $exe
$shortcut.Arguments = '/setup'
$shortcut.Save()
Write-Host "바로 가기: $shortcutPath"

Write-Host ''
Write-Host '설치 완료. 자녀 계정으로 다시 로그인하면 적용됩니다.' -ForegroundColor Green
