// ChildrenBlockService.exe
//
//   (인수 없음)            Windows 서비스로 실행 (서비스 관리자가 시작)
//   --dryrun               PC 종료·세션 잠금을 실제로 하지 않음
//   --console              콘솔에서 현재 사용자로 실행 (개발·테스트용, 기본 --dryrun)
//     --child-all          모든 계정을 자녀로 취급
//     --speed N            사용 시간 N 배속 (1~600)
//     --unprotected-tray   트레이 프로세스 보호를 끔 (재실행 테스트용)
//     --real-shutdown      콘솔 모드에서도 실제로 PC 종료

#include "pch.h"

#include "Common.h"
#include "Controller.h"

#include <cstdio>
#include <fcntl.h>
#include <io.h>

namespace {

ControllerOptions g_options;
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status = {};
HANDLE g_stopEvent = nullptr;
Controller* g_controller = nullptr;

void ParseOptions(int argc, wchar_t** argv, bool& console) {
    bool realShutdown = false;
    for (int i = 1; i < argc; ++i) {
        CString arg(argv[i]);
        arg.MakeLower();
        if (arg == _T("--console")) {
            console = true;
        } else if (arg == _T("--dryrun")) {
            g_options.dryRun = true;
        } else if (arg == _T("--child-all")) {
            g_options.everyoneIsChild = true;
        } else if (arg == _T("--unprotected-tray")) {
            g_options.protectTray = false;
        } else if (arg == _T("--real-shutdown")) {
            realShutdown = true;
        } else if (arg == _T("--speed") && i + 1 < argc) {
            const int speed = _wtoi(argv[++i]);
            g_options.speed = speed < 1 ? 1 : (speed > 600 ? 600 : speed);
        }
    }
    if (console) {
        g_options.asService = false;
        if (!realShutdown) g_options.dryRun = true;  // 개발 PC 를 실수로 끄지 않도록
    } else {
        g_options.speed = 1;
        g_options.everyoneIsChild = false;
        g_options.protectTray = true;
    }
}

void SetState(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    static DWORD checkPoint = 1;
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = waitHint;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN : 0;
    g_status.dwCheckPoint = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkPoint++;
    SetServiceStatus(g_statusHandle, &g_status);
}

DWORD WINAPI ControlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
        SetState(SERVICE_STOP_PENDING, NO_ERROR, 15000);
        SetEvent(g_stopEvent);
        return NO_ERROR;
    case SERVICE_CONTROL_PRESHUTDOWN:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_controller) g_controller->Flush();
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI ServiceMain(DWORD, LPTSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerEx(kServiceName, ControlHandler, nullptr);
    if (!g_statusHandle) return;
    SetState(SERVICE_START_PENDING, NO_ERROR, 10000);

    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    Controller controller(g_options);
    if (!controller.Start()) {
        SetState(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }
    g_controller = &controller;
    SetState(SERVICE_RUNNING);

    WaitForSingleObject(g_stopEvent, INFINITE);
    controller.Stop();
    g_controller = nullptr;
    SetState(SERVICE_STOPPED);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD) {
    SetEvent(g_stopEvent);
    return TRUE;
}

int RunConsole() {
    _setmode(_fileno(stdout), _O_U16TEXT);
    wprintf(L"ChildrenBlock 서비스 (콘솔 모드)%s - Ctrl+C 로 종료\n", g_options.dryRun ? L" [테스트 모드]" : L"");
    wprintf(L"데이터: %s\n파이프: %s\n", GetDataDir().GetString(), proto::PipeName().GetString());

    // Ctrl+C 또는 이름 있는 이벤트(테스트 스크립트용)로 종료
    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, _T("Local\\ChildrenBlock.ConsoleStop"));
    ResetEvent(g_stopEvent);  // 이전 실행에서 신호된 채 남아 있을 수 있음 (다른 프로세스가 핸들을 쥐고 있으면)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    Controller controller(g_options);
    if (!controller.Start()) {
        wprintf(L"시작 실패 (같은 파이프를 쓰는 서비스가 이미 실행 중일 수 있습니다)\n");
        return 1;
    }
    WaitForSingleObject(g_stopEvent, INFINITE);
    controller.Stop();
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (!AfxWinInit(::GetModuleHandle(nullptr), nullptr, ::GetCommandLine(), 0)) return 1;

    bool console = false;
    ParseOptions(argc, argv, console);
    if (console) return RunConsole();

    SERVICE_TABLE_ENTRY table[] = {{const_cast<LPTSTR>(kServiceName), ServiceMain}, {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcher(table)) {
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            _setmode(_fileno(stdout), _O_U16TEXT);
            wprintf(L"이 프로그램은 Windows 서비스입니다. installer\\install.ps1 로 설치하세요.\n"
                    L"테스트: ChildrenBlockService.exe --console [--child-all] [--speed N]\n");
        }
        return 1;
    }
    return 0;
}
