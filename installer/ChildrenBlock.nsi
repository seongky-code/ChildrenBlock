; ChildrenBlock 설치 프로그램 (NSIS 3, 유니코드)
;
; 빌드:  makensis installer\ChildrenBlock.nsi
;        (먼저 ChildrenBlock.sln 을 Release | x64 로 빌드)
; 결과:  bin\ChildrenBlockSetup-<버전>.exe
;
; 설치 내용 (install.ps1 과 동일)
;   1. 실행 파일(서비스 + 트레이)을 Program Files\ChildrenBlock 에 복사
;   2. %ProgramData%\ChildrenBlock 권한: SYSTEM·관리자만 접근
;   3. 제한할 계정을 settings.ini 에 기록
;   4. ChildrenBlock 서비스 등록 (자동 시작, 실패 시 재시작) 후 시작
;   5. 부모 비밀번호 등록 및 초기 설정 (ChildrenBlock.exe /setup)
;   6. 시작 메뉴 바로 가기, 프로그램 추가/제거 등록
;
; 자동 설치:  ChildrenBlockSetup.exe /S /USERS=민준,서연 [/DRYRUN]
;             (/S 에서는 비밀번호 등록을 건너뛰므로 설치 후 관리자 권한으로 "ChildrenBlock 설정" 실행)

Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define APPNAME     "ChildrenBlock"
!define VERSION     "2.0.0"
!define PUBLISHER   "ChildrenBlock"
!define SERVICE     "ChildrenBlock"
!define UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
!define BIN_DIR     "..\bin\x64\Release"

Name "${APPNAME}"
OutFile "..\bin\${APPNAME}Setup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1042 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1042 "FileDescription" "${APPNAME} 설치 프로그램"
VIAddVersionKey /LANG=1042 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1042 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1042 "LegalCopyright" "${PUBLISHER}"

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"
${StrStr}

; ---------------------------------------------------------------------------
; 화면

!define MUI_ICON   "..\src\res\ChildrenBlock.ico"
!define MUI_UNICON "..\src\res\ChildrenBlock.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TEXT "자녀 PC 사용 시간 관리 프로그램을 설치합니다.$\r$\n$\r$\n\
- 사용/휴식 주기와 휴식 1분 전 알림$\r$\n\
- 하루 사용 시간 · 종료 시각에 PC 자동 종료$\r$\n\
- 자녀 계정에서 끌 수 없는 Windows 서비스로 동작$\r$\n$\r$\n\
설치가 끝나면 부모 비밀번호와 초기 설정을 입력합니다."
!insertmacro MUI_PAGE_WELCOME
Page custom AccountPageCreate AccountPageLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "설치가 끝났습니다.$\r$\n$\r$\n\
대상 계정이 로그인해 있으면 몇 초 안에 트레이 아이콘이 나타납니다.$\r$\n\
설정 변경은 시작 메뉴의 'ChildrenBlock 설정'에서 할 수 있습니다."
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Korean"

; ---------------------------------------------------------------------------
; 변수

Var Users        ; 제한할 계정 (쉼표 구분)
Var DryRun       ; 1 이면 PC 종료·세션 잠금 안 함
Var DataDir
Var hUsersEdit
Var hDryRunCheck

; ---------------------------------------------------------------------------
; 초기화

; 지금 화면에 로그인한 사용자 (설치 프로그램을 다른 관리자 계정으로 승격했어도 정확함)
Function DetectConsoleUser
    StrCpy $Users ""
    System::Call 'kernel32::WTSGetActiveConsoleSessionId()i.r0'
    System::Call 'wtsapi32::WTSQuerySessionInformationW(p0, ir0, i5, *p.r1, *i.r2)i.r3'
    ${If} $3 <> 0
        System::Call '*$1(&w256.r4)'
        StrCpy $Users $4
        System::Call 'wtsapi32::WTSFreeMemory(pr1)'
    ${EndIf}
    ${If} $Users == ""
        ReadEnvStr $Users "USERNAME"
    ${EndIf}
FunctionEnd

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "64비트 Windows 10/11 에서만 설치할 수 있습니다."
        Abort
    ${EndIf}
    SetRegView 64

    Call DetectConsoleUser
    StrCpy $DryRun 0

    ; 명령줄: /USERS=a,b  /DRYRUN
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/USERS=" $1
    ${IfNot} ${Errors}
        StrCpy $Users $1
    ${EndIf}
    ClearErrors
    ${GetOptions} $0 "/DRYRUN" $1
    ${IfNot} ${Errors}
        StrCpy $DryRun 1
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; 계정 선택 화면

Function AccountPageCreate
    !insertmacro MUI_HEADER_TEXT "제한할 계정" "사용 시간 제한을 적용할 Windows 계정을 지정합니다."

    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 24u "계정 이름을 입력하세요. 여러 명이면 쉼표(,)로 구분합니다.$\r$\n기본값은 지금 로그인한 계정입니다."
    Pop $0
    ${NSD_CreateText} 0 28u 100% 13u $Users
    Pop $hUsersEdit

    ${NSD_CreateLabel} 0 52u 100% 36u "자녀 계정은 반드시 '표준 사용자'여야 합니다.$\r$\n관리자 계정은 서비스를 멈출 수 있어 제한을 우회할 수 있습니다.$\r$\n(본인 계정에서 시험할 때는 아래 테스트 모드를 권장합니다)"
    Pop $0

    ${NSD_CreateCheckbox} 0 96u 100% 12u "테스트 모드 - 휴식·알림은 동작하지만 PC 종료와 세션 잠금은 하지 않음"
    Pop $hDryRunCheck
    ${If} $DryRun == 1
        ${NSD_Check} $hDryRunCheck
    ${EndIf}

    nsDialogs::Show
FunctionEnd

Function AccountPageLeave
    ${NSD_GetText} $hUsersEdit $Users
    ${NSD_GetState} $hDryRunCheck $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $DryRun 1
    ${Else}
        StrCpy $DryRun 0
    ${EndIf}

    ${If} $Users == ""
        MessageBox MB_ICONEXCLAMATION "제한할 계정 이름을 입력하세요."
        Abort
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; 설치

!macro Run COMMAND
    nsExec::ExecToLog `${COMMAND}`
    Pop $0
!macroend

Section "Install"
    SetShellVarContext all
    SetRegView 64
    ${DisableX64FSRedirection}
    StrCpy $DataDir "$APPDATA\${APPNAME}"

    ; 1. 기존 버전 정리: 서비스를 멈추면 서비스가 트레이도 함께 끝낸다.
    DetailPrint "기존 서비스 중지..."
    !insertmacro Run 'net.exe stop ${SERVICE}'
    !insertmacro Run 'taskkill.exe /f /im ChildrenBlock.exe'
    !insertmacro Run 'taskkill.exe /f /im ChildrenBlockService.exe'
    ; 1단계 버전이 만든 작업 스케줄러 항목 제거
    !insertmacro Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Get-ScheduledTask -TaskName 'ChildrenBlock*' -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$$false"`

    ; 2. 파일 복사
    SetOutPath "$INSTDIR"
    File "${BIN_DIR}\ChildrenBlock.exe"
    File "${BIN_DIR}\ChildrenBlockService.exe"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; 3. 데이터 폴더: 상속을 끊고 SYSTEM·관리자만 접근 (기존 파일의 개별 권한도 초기화)
    DetailPrint "데이터 폴더 권한 설정: $DataDir"
    CreateDirectory "$DataDir"
    !insertmacro Run 'icacls.exe "$DataDir" /reset /T /C /Q'
    !insertmacro Run 'icacls.exe "$DataDir" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F /Q'
    ${If} $0 != 0
        MessageBox MB_ICONEXCLAMATION "데이터 폴더 권한을 설정하지 못했습니다 (코드 $0).$\r$\n$DataDir" /SD IDOK
    ${EndIf}

    ; 4. 제한할 계정
    WriteINIStr "$DataDir\settings.ini" "Children" "Users" "$Users"
    DetailPrint "제한 대상 계정: $Users"

    ; 5. 서비스 등록 (있으면 경로만 갱신)
    StrCpy $1 '\"$INSTDIR\ChildrenBlockService.exe\"'
    ${If} $DryRun == 1
        StrCpy $1 '$1 --dryrun'
        DetailPrint "테스트 모드: PC 종료와 세션 잠금을 하지 않습니다."
    ${EndIf}
    nsExec::ExecToLog 'sc.exe query ${SERVICE}'
    Pop $0
    ${If} $0 == 0
        !insertmacro Run 'sc.exe config ${SERVICE} binPath= "$1" start= auto'
    ${Else}
        !insertmacro Run 'sc.exe create ${SERVICE} binPath= "$1" start= auto DisplayName= "${APPNAME}"'
    ${EndIf}
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "서비스를 등록하지 못했습니다 (코드 $0)." /SD IDOK
        Abort
    ${EndIf}
    !insertmacro Run 'sc.exe description ${SERVICE} "자녀 PC 사용 시간 관리 (사용/휴식 주기, 자동 종료)"'
    !insertmacro Run 'sc.exe failure ${SERVICE} reset= 86400 actions= restart/3000/restart/3000/restart/10000'

    DetailPrint "서비스 시작..."
    !insertmacro Run 'sc.exe start ${SERVICE}'
    ; 실행 중이 될 때까지 최대 10초 대기 (설정 화면이 서비스에 연결해야 함)
    StrCpy $3 0
    ${Do}
        nsExec::ExecToStack 'sc.exe query ${SERVICE}'
        Pop $0
        Pop $1
        ${StrStr} $2 $1 "RUNNING"
        ${If} $2 != ""
            ${Break}
        ${EndIf}
        IntOp $3 $3 + 1
        ${If} $3 >= 20
            MessageBox MB_ICONEXCLAMATION "서비스가 시작되지 않았습니다. 이벤트 뷰어와 $DataDir\log.txt 를 확인하세요." /SD IDOK
            ${Break}
        ${EndIf}
        Sleep 500
    ${Loop}

    ; 6. 시작 메뉴
    CreateShortcut "$SMPROGRAMS\ChildrenBlock 설정.lnk" "$INSTDIR\ChildrenBlock.exe" "/setup" "$INSTDIR\ChildrenBlock.exe" 0
    CreateShortcut "$SMPROGRAMS\ChildrenBlock 사용 기록.lnk" "$INSTDIR\ChildrenBlock.exe" "/history" "$INSTDIR\ChildrenBlock.exe" 0

    ; 7. 프로그램 추가/제거
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\ChildrenBlock.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"

    ; 8. 부모 비밀번호·초기 설정 (설치 프로그램이 관리자 권한이라 최초 등록 가능)
    ${EnableX64FSRedirection}
    ${IfNot} ${Silent}
        DetailPrint "부모 비밀번호와 초기 설정..."
        ExecWait '"$INSTDIR\ChildrenBlock.exe" /setup'
        ReadINIStr $0 "$DataDir\settings.ini" "Security" "Hash"
        ${If} $0 == ""
            MessageBox MB_ICONEXCLAMATION "비밀번호가 등록되지 않았습니다.$\r$\n시작 메뉴의 'ChildrenBlock 설정'을 마우스 오른쪽 단추 > '관리자 권한으로 실행'해 등록하세요."
        ${EndIf}
    ${EndIf}
SectionEnd

; ---------------------------------------------------------------------------
; 제거

Function un.onInit
    SetRegView 64
FunctionEnd

Section "Uninstall"
    SetShellVarContext all
    SetRegView 64
    ${DisableX64FSRedirection}
    StrCpy $DataDir "$APPDATA\${APPNAME}"

    ; 서비스를 멈추면 서비스가 트레이도 함께 끝낸다.
    !insertmacro Run 'net.exe stop ${SERVICE}'
    !insertmacro Run 'sc.exe delete ${SERVICE}'
    !insertmacro Run 'taskkill.exe /f /im ChildrenBlock.exe'
    !insertmacro Run 'taskkill.exe /f /im ChildrenBlockService.exe'
    !insertmacro Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Get-ScheduledTask -TaskName 'ChildrenBlock*' -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$$false"`
    ${EnableX64FSRedirection}
    Sleep 1000

    Delete "$SMPROGRAMS\ChildrenBlock 설정.lnk"
    Delete "$SMPROGRAMS\ChildrenBlock 사용 기록.lnk"

    Delete "$INSTDIR\ChildrenBlock.exe"
    Delete "$INSTDIR\ChildrenBlockService.exe"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${UNINST_KEY}"

    ; 자동 제거(/S)에서는 기록을 남긴다.
    MessageBox MB_YESNO|MB_ICONQUESTION "설정과 사용 기록($DataDir)도 삭제할까요?" /SD IDNO IDNO keep_data
        RMDir /r "$DataDir"
    keep_data:
SectionEnd
