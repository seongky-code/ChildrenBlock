#Requires -RunAsAdministrator
<#
.SYNOPSIS
    ChildrenBlock 설치 (관리자 PowerShell 에서 실행)

.DESCRIPTION
    1. 실행 파일(서비스 + 트레이)을 Program Files\ChildrenBlock 에 복사
    2. %ProgramData%\ChildrenBlock 권한 설정: SYSTEM·관리자만 접근 (자녀 계정은 읽기도 불가)
    3. 제한할 자녀 계정을 settings.ini 에 기록
    4. ChildrenBlock 서비스 등록 (자동 시작, 비정상 종료 시 재시작) 후 시작
       - 서비스가 자녀 계정 로그온을 감지해 트레이 앱을 띄우고 감시한다
    5. 부모 비밀번호 등록 및 초기 설정 (ChildrenBlock.exe /setup)
    6. 시작 메뉴에 'ChildrenBlock 설정', 'ChildrenBlock 사용 기록' 바로 가기 생성

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1
    (계정을 지정하지 않으면 지금 Windows 에 로그인한 사용자에게 적용)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -DryRun
    (현재 사용자에게 설치하되 PC 종료·세션 잠금은 하지 않는 테스트 모드)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1 -ChildUser 민준,서연
#>
param(
    # 제한을 적용할 Windows 계정 이름 (여러 명이면 쉼표로 구분, "도메인\이름" 도 가능)
    # 생략하면 현재 로그인한 사용자
    [string[]]$ChildUser,

    # 빌드 결과 폴더 (ChildrenBlock.exe, ChildrenBlockService.exe 가 있는 곳)
    [string]$SourceDir = (Join-Path $PSScriptRoot '..\bin\x64\Release'),

    # 테스트 모드: 사용 시간·휴식은 그대로 동작하지만 PC 종료와 세션 잠금은 하지 않음
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$serviceName = 'ChildrenBlock'
$installDir = Join-Path $env:ProgramFiles 'ChildrenBlock'
$dataDir = Join-Path $env:ProgramData 'ChildrenBlock'
$trayExe = Join-Path $installDir 'ChildrenBlock.exe'
$serviceExe = Join-Path $installDir 'ChildrenBlockService.exe'
$startMenu = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'

foreach ($name in 'ChildrenBlock.exe', 'ChildrenBlockService.exe') {
    if (-not (Test-Path (Join-Path $SourceDir $name))) {
        throw "실행 파일이 없습니다: $(Join-Path $SourceDir $name)`n먼저 Release 로 빌드하세요."
    }
}

if (-not $ChildUser) {
    # 관리자 PowerShell 을 다른 관리자 계정으로 승격했을 수 있으므로 $env:USERNAME 대신
    # 지금 화면에 로그인한 사용자를 쓴다.
    $interactive = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $interactive) { $interactive = "$env:USERDOMAIN\$env:USERNAME" }
    $ChildUser = @($interactive)
    Write-Host "계정을 지정하지 않아 현재 로그인한 사용자에게 설치합니다: $interactive" -ForegroundColor Yellow
    if (-not $DryRun) {
        Write-Warning '이 계정에서 휴식 화면과 PC 자동 종료가 적용됩니다. 시험용이라면 -DryRun 을 붙이세요.'
    }
}

# 서비스는 도메인 없이 계정 이름만 비교한다.
$childNames = @()
foreach ($entry in $ChildUser) {
    if ($entry -match '\\') { $domain, $name = $entry -split '\\', 2 } else { $domain, $name = $env:COMPUTERNAME, $entry }

    if ($domain -ieq $env:COMPUTERNAME) {
        if (-not (Get-LocalUser -Name $name -ErrorAction SilentlyContinue)) {
            throw "로컬 계정 '$name' 을(를) 찾을 수 없습니다."
        }
    } else {
        Write-Host "도메인 계정 $domain\$name - 로컬 계정 확인은 건너뜁니다."
    }

    try {
        # 연결이 끊긴 SID 가 그룹에 있으면 Get-LocalGroupMember 가 실패하므로 경고 확인만 건너뛴다.
        if ((Get-LocalGroupMember -SID 'S-1-5-32-544' | Where-Object { $_.Name -like "*\$name" })) {
            Write-Warning "'$name' 는 관리자 계정입니다. 관리자 계정은 서비스를 멈출 수 있으므로 자녀용이라면 표준 사용자로 바꾸세요."
        }
    } catch {
        Write-Warning "관리자 그룹을 확인하지 못했습니다. '$name' 가 표준 사용자인지 직접 확인하세요."
    }
    $childNames += $name
}

# 1. 기존 설치 정리 (1단계 버전의 작업 스케줄러 항목 포함)
if (Get-Service $serviceName -ErrorAction SilentlyContinue) {
    Stop-Service $serviceName -Force -ErrorAction SilentlyContinue
    sc.exe delete $serviceName | Out-Null
    Start-Sleep 2
}
Get-ScheduledTask -TaskName 'ChildrenBlock*' -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$false
Get-Process ChildrenBlock, ChildrenBlockService -ErrorAction SilentlyContinue | Stop-Process -Force

New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item (Join-Path $SourceDir 'ChildrenBlock.exe') $trayExe -Force
Copy-Item (Join-Path $SourceDir 'ChildrenBlockService.exe') $serviceExe -Force
Write-Host "설치: $installDir"

# 2. 데이터 폴더 권한: SYSTEM·관리자만
New-Item -ItemType Directory -Force $dataDir | Out-Null
$system = New-Object Security.Principal.SecurityIdentifier 'S-1-5-18'
$admins = New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-544'
$inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
$prop = [Security.AccessControl.PropagationFlags]::None
$allow = [Security.AccessControl.AccessControlType]::Allow
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetAccessRuleProtection($true, $false)  # 상속 끊기
$acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($system, 'FullControl', $inherit, $prop, $allow)))
$acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($admins, 'FullControl', $inherit, $prop, $allow)))
Set-Acl $dataDir $acl
Get-ChildItem $dataDir -File | ForEach-Object {
    # 1단계 버전에서 만든 파일의 개별 권한 제거
    $fileAcl = Get-Acl $_.FullName
    $fileAcl.SetAccessRuleProtection($false, $false)
    $fileAcl.Access | Where-Object { -not $_.IsInherited } | ForEach-Object { $fileAcl.RemoveAccessRule($_) | Out-Null }
    Set-Acl $_.FullName $fileAcl
}
Write-Host "권한 설정: $dataDir (SYSTEM, Administrators 만 접근)"

# 3. 자녀 계정 기록
Add-Type -Namespace CbInstall -Name Ini -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
'@
if (-not [CbInstall.Ini]::WritePrivateProfileString('Children', 'Users', ($childNames -join ','), (Join-Path $dataDir 'settings.ini'))) {
    throw 'settings.ini 에 자녀 계정을 기록하지 못했습니다.'
}
Write-Host "제한 대상 계정: $($childNames -join ', ')"

# 4. 서비스 등록·시작
$binaryPath = "`"$serviceExe`""
if ($DryRun) {
    $binaryPath += ' --dryrun'
    Write-Host '테스트 모드: PC 종료와 세션 잠금은 하지 않습니다.' -ForegroundColor Yellow
}
New-Service -Name $serviceName -BinaryPathName $binaryPath -DisplayName 'ChildrenBlock' `
    -Description '자녀 PC 사용 시간 관리 (사용/휴식 주기, 자동 종료)' -StartupType Automatic | Out-Null
sc.exe failure $serviceName reset= 86400 actions= restart/3000/restart/3000/restart/10000 | Out-Null
Start-Service $serviceName
Write-Host "서비스 시작: $serviceName"

# 5. 부모 비밀번호·설정 (관리자 권한으로 실행되므로 최초 비밀번호 등록 가능)
Write-Host '부모 비밀번호와 초기 설정을 입력하세요...'
Start-Process $trayExe -ArgumentList '/setup' -Wait
if (-not (Select-String -Path (Join-Path $dataDir 'settings.ini') -Pattern '^Hash=.+' -Quiet)) {
    Write-Warning "비밀번호가 등록되지 않았습니다. 관리자 권한으로 '$trayExe /setup' 을 실행해 등록하세요."
}

# 6. 시작 메뉴 바로 가기
$shell = New-Object -ComObject WScript.Shell
foreach ($item in @(@('ChildrenBlock 설정', '/setup'), @('ChildrenBlock 사용 기록', '/history'))) {
    $shortcut = $shell.CreateShortcut((Join-Path $startMenu "$($item[0]).lnk"))
    $shortcut.TargetPath = $trayExe
    $shortcut.Arguments = $item[1]
    $shortcut.Save()
}
Write-Host "바로 가기: 시작 메뉴 > ChildrenBlock 설정 / ChildrenBlock 사용 기록"

Write-Host ''
Write-Host '설치 완료. 대상 계정이 이미 로그인해 있으면 몇 초 안에 트레이 아이콘이 나타납니다.' -ForegroundColor Green
Write-Host "제거: powershell -ExecutionPolicy Bypass -File `"$PSScriptRoot\uninstall.ps1`""
