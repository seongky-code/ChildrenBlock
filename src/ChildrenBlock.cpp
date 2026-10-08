#include "pch.h"
#include "ChildrenBlock.h"

#include "Common.h"
#include "HistoryDlg.h"
#include "PasswordDlg.h"
#include "SettingsDlg.h"
#include "UiUtil.h"

CChildrenBlockApp theApp;

// 명령줄 옵션
//   (없음)           트레이 앱 (보통은 서비스가 /agent 로 자녀 세션에 실행)
//   /setup           비밀번호 등록·설정 화면 (부모 계정에서 사용, 최초 등록은 관리자 권한 필요)
//   /history         사용 기록 화면 (비밀번호 불필요)
//   /resetpassword   비밀번호 초기화 (관리자 권한 필요)
CChildrenBlockApp::CommandLine CChildrenBlockApp::ParseCommandLine() {
    CommandLine cmd;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return cmd;

    for (int i = 1; i < argc; ++i) {
        CString arg(argv[i]);
        arg.MakeLower();
        if (arg == _T("/agent"))
            cmd.agent = true;
        else if (arg == _T("/setup"))
            cmd.setup = true;
        else if (arg == _T("/history"))
            cmd.history = true;
        else if (arg == _T("/resetpassword"))
            cmd.resetPassword = true;
    }
    LocalFree(argv);
    return cmd;
}

void CChildrenBlockApp::RunSetup() {
    proto::Message status;
    if (!CallService(nullptr, proto::Message(proto::kStatus), &status)) return;

    CString pw;
    if (status.GetInt(_T("hasPassword")) == 0) {
        CSetupPasswordDlg dlg(_T("처음 설정입니다. 부모용 비밀번호를 등록하세요.\n설정 변경과 제한 해제에 사용됩니다."));
        if (dlg.DoModal() != IDOK) return;
        proto::Message init(proto::kInitPassword);
        init.Set(_T("newpw"), dlg.m_password);
        if (!CallService(nullptr, init)) return;
        pw = dlg.m_password;
    } else {
        if (!AskPassword(nullptr, _T("설정을 변경하려면 비밀번호를 입력하세요."), pw)) return;
        proto::Message verify(proto::kVerify);
        verify.Set(_T("pw"), pw);
        if (!CallService(nullptr, verify)) return;
    }

    proto::Message config;
    if (!CallService(nullptr, proto::Message(proto::kConfig), &config)) return;
    CSettingsDlg dlg(proto::ConfigFromMessage(config), -1);
    if (dlg.DoModal() != IDOK) return;

    proto::Message set(proto::kSetConfig);
    set.Set(_T("pw"), pw);
    proto::ConfigToMessage(dlg.m_cfg, set);
    if (CallService(nullptr, set))
        ShowMessage(nullptr, _T("저장되었습니다.\n자녀 계정에 바로 적용됩니다."), MB_ICONINFORMATION);
}

void CChildrenBlockApp::ResetPassword() {
    if (CallService(nullptr, proto::Message(proto::kResetPassword))) {
        ShowMessage(nullptr, _T("비밀번호가 초기화되었습니다.\n관리자 권한으로 ChildrenBlock /setup 을 실행해 새 비밀번호를 등록하세요."),
                    MB_ICONINFORMATION);
    }
}

BOOL CChildrenBlockApp::InitInstance() {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&icc);
    CWinApp::InitInstance();

    const CommandLine cmd = ParseCommandLine();
    if (cmd.resetPassword) {
        ResetPassword();
        return FALSE;
    }
    if (cmd.setup) {
        RunSetup();
        return FALSE;
    }
    if (cmd.history) {
        CHistoryDlg::Show();
        return FALSE;
    }

    m_hMutex = CreateMutex(nullptr, TRUE, _T("Local\\ChildrenBlock.SingleInstance"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!cmd.agent) {
            ShowMessage(nullptr, _T("ChildrenBlock 이 이미 실행 중입니다.\n작업 표시줄 오른쪽 아래 트레이 아이콘을 확인하세요."),
                        MB_ICONINFORMATION);
        }
        m_exitCode = kExitAlreadyRunning;  // 서비스가 강제 종료로 오인하지 않도록
        return FALSE;
    }

    auto* wnd = new CTrayWnd;
    if (!wnd->CreateWnd()) {
        delete wnd;
        ShowMessage(nullptr, _T("트레이 창을 만들지 못했습니다."), MB_ICONERROR);
        return FALSE;
    }
    m_pMainWnd = wnd;
    return TRUE;
}

int CChildrenBlockApp::ExitInstance() {
    if (m_hMutex) {
        ReleaseMutex(m_hMutex);
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }
    const int code = CWinApp::ExitInstance();
    return m_exitCode != 0 ? m_exitCode : code;
}
