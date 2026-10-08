#include "pch.h"
#include "ChildrenBlock.h"

#include "Common.h"
#include "HistoryDlg.h"
#include "PasswordDlg.h"
#include "PasswordHasher.h"
#include "SettingsDlg.h"

CChildrenBlockApp theApp;

// 명령줄 옵션
//   /setup           트레이 없이 비밀번호 등록·설정 화면만 띄우고 종료 (부모 계정에서 사용)
//   /history         사용 기록 화면만 띄우고 종료 (비밀번호 불필요)
//   /resetpassword   비밀번호 초기화 (관리자 권한 필요)
//   /dryrun          실제로 PC 를 끄지 않음 (테스트용)
//   /speed:N         사용 시간을 N 배속으로 진행 (테스트용, 1~600)
CChildrenBlockApp::CommandLine CChildrenBlockApp::ParseCommandLine() {
    CommandLine cmd;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return cmd;

    for (int i = 1; i < argc; ++i) {
        CString arg(argv[i]);
        arg.MakeLower();
        if (arg == _T("/setup")) {
            cmd.setup = true;
        } else if (arg == _T("/history")) {
            cmd.history = true;
        } else if (arg == _T("/resetpassword")) {
            cmd.resetPassword = true;
        } else if (arg == _T("/dryrun")) {
            cmd.options.dryRun = true;
        } else if (arg.Left(7) == _T("/speed:")) {
            const int speed = _ttoi(arg.Mid(7));
            cmd.options.speed = speed < 1 ? 1 : (speed > 600 ? 600 : speed);
        }
    }
    LocalFree(argv);
    return cmd;
}

bool CChildrenBlockApp::EnsurePassword() {
    if (m_settings.HasPassword()) return true;

    CSetupPasswordDlg dlg(_T("처음 실행입니다. 부모용 비밀번호를 등록하세요.\n설정 변경과 프로그램 종료에 사용됩니다."));
    if (dlg.DoModal() != IDOK) return false;

    if (!PasswordHasher::Create(dlg.m_password, m_settings.passwordSalt, m_settings.passwordHash,
                                m_settings.passwordIterations) ||
        !SettingsStore::Save(m_settings)) {
        AfxMessageBox(_T("비밀번호를 저장하지 못했습니다.\n") + GetDataDir() + _T(" 폴더 권한을 확인하세요."),
                      MB_ICONERROR);
        return false;
    }
    LogEvent(_T("비밀번호 최초 등록"));
    return true;
}

void CChildrenBlockApp::RunSetup() {
    if (m_settings.HasPassword()) {
        CPasswordDlg pw(_T("설정을 변경하려면 비밀번호를 입력하세요."));
        if (pw.DoModal() != IDOK) return;
        if (!PasswordHasher::Verify(pw.m_password, m_settings.passwordSalt, m_settings.passwordHash,
                                    m_settings.passwordIterations)) {
            LogEvent(_T("[설정 모드] 비밀번호 틀림"));
            AfxMessageBox(_T("비밀번호가 올바르지 않습니다."), MB_ICONWARNING);
            return;
        }
    } else if (!EnsurePassword()) {
        return;
    }

    SavedState state;
    SYSTEMTIME st;
    GetLocalTime(&st);
    const int today = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
    const int64_t usedToday = SettingsStore::LoadState(state) && state.date == today ? state.engine.usedTodayMs : 0;

    CSettingsDlg dlg(m_settings.schedule, usedToday);
    if (dlg.DoModal() != IDOK) return;

    m_settings.schedule = dlg.m_cfg;
    if (!SettingsStore::Save(m_settings)) {
        AfxMessageBox(_T("설정을 저장하지 못했습니다.\n관리자 권한으로 실행했는지 확인하세요."), MB_ICONERROR);
        return;
    }
    LogEvent(_T("[설정 모드] 설정 저장"));
    AfxMessageBox(_T("저장되었습니다.\n실행 중인 ChildrenBlock 에도 몇 초 안에 적용됩니다."), MB_ICONINFORMATION);
}

void CChildrenBlockApp::ResetPassword() {
    if (!IsUserAnAdmin()) {
        AfxMessageBox(_T("비밀번호 초기화는 관리자 권한으로 실행해야 합니다."), MB_ICONWARNING);
        return;
    }
    m_settings.passwordSalt.Empty();
    m_settings.passwordHash.Empty();
    m_settings.passwordIterations = 0;
    SettingsStore::Save(m_settings);
    LogEvent(_T("비밀번호 초기화 (관리자)"));
    AfxMessageBox(_T("비밀번호가 초기화되었습니다.\nChildrenBlock /setup 으로 새 비밀번호를 등록하세요."),
                  MB_ICONINFORMATION);
}

BOOL CChildrenBlockApp::InitInstance() {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&icc);
    CWinApp::InitInstance();

    const CommandLine cmd = ParseCommandLine();
    SettingsStore::Load(m_settings);

    if (cmd.resetPassword) {
        ResetPassword();
        return FALSE;
    }
    if (cmd.history) {
        CHistoryDlg::Show(m_settings.schedule, nullptr);
        return FALSE;
    }
    if (cmd.setup) {
        RunSetup();
        return FALSE;
    }

    m_hMutex = CreateMutex(nullptr, TRUE, _T("Local\\ChildrenBlock.SingleInstance"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        AfxMessageBox(_T("ChildrenBlock 이 이미 실행 중입니다.\n작업 표시줄 오른쪽 아래 트레이 아이콘을 확인하세요."),
                      MB_ICONINFORMATION);
        return FALSE;
    }

    if (!EnsurePassword()) return FALSE;

    auto* wnd = new CTrayWnd(m_settings, cmd.options);
    if (!wnd->CreateWnd()) {
        delete wnd;
        AfxMessageBox(_T("트레이 창을 만들지 못했습니다."), MB_ICONERROR);
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
    return CWinApp::ExitInstance();
}
