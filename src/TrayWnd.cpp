#include "pch.h"
#include "TrayWnd.h"

#include "Common.h"
#include "HistoryDlg.h"
#include "PasswordDlg.h"
#include "SettingsDlg.h"
#include "UiUtil.h"

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTrayId = 1;
constexpr DWORD kPollTimeoutMs = 1500;

enum : UINT {
    ID_TRAY_STATUS = 40001,
    ID_TRAY_STATUS2,
    ID_TRAY_HISTORY,
    ID_TRAY_SETTINGS,
    ID_TRAY_SKIPBREAK,
    ID_TRAY_EXTEND15,
    ID_TRAY_EXTEND30,
    ID_TRAY_EXTEND60,
    ID_TRAY_PAUSE,
    ID_TRAY_CHANGEPW,
    ID_TRAY_CLOSE,
};

// 알림 강조색 (트레이 아이콘 색과 맞춤)
const COLORREF kColorBreakSoon = RGB(240, 176, 72);
const COLORREF kColorShutdown = RGB(236, 98, 98);

const UINT s_msgTaskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));

}  // namespace

BEGIN_MESSAGE_MAP(CTrayWnd, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_CLOSE()
    ON_WM_TIMER()
    ON_MESSAGE(WM_APP_TRAY, &CTrayWnd::OnTrayNotify)
    ON_MESSAGE(WM_APP_UNLOCK_REQUEST, &CTrayWnd::OnUnlockRequest)
    ON_REGISTERED_MESSAGE(s_msgTaskbarCreated, &CTrayWnd::OnTaskbarCreated)
END_MESSAGE_MAP()

BOOL CTrayWnd::CreateWnd() {
    const CString cls = AfxRegisterWndClass(0);
    return CreateEx(0, cls, _T("ChildrenBlockTrayWnd"), WS_POPUP, 0, 0, 0, 0, nullptr, 0);
}

void CTrayWnd::PostNcDestroy() { delete this; }

// ---------------------------------------------------------------------------
// 수명 주기

int CTrayWnd::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;

    const int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    const UINT iconIds[IconCount] = {IDR_MAINFRAME, IDI_TRAY_BREAKSOON, IDI_TRAY_BREAK, IDI_TRAY_SHUTDOWN};
    for (int i = 0; i < IconCount; ++i) {
        m_icons[i] = static_cast<HICON>(
            LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(iconIds[i]), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    }

    if (!m_warning.CreateWnd() || !m_overlay.CreateWnd(this)) return -1;

    AddTrayIcon();
    SetTimer(kTimerId, 1000, nullptr);
    Poll();
    return 0;
}

void CTrayWnd::OnDestroy() {
    KillTimer(kTimerId);
    if (m_iconAdded) Shell_NotifyIcon(NIM_DELETE, &m_nid);
    m_warning.DestroyWindow();
    m_overlay.DestroyWindow();
    for (HICON& icon : m_icons) {
        if (icon) DestroyIcon(icon);
        icon = nullptr;
    }
    CWnd::OnDestroy();
}

void CTrayWnd::OnClose() {
    // taskkill /im 처럼 WM_CLOSE 로 끄려는 시도는 무시한다.
    if (!m_exitAuthorized) return;
    DestroyWindow();  // 메인 창이 파괴되면 메시지 루프가 끝난다
}

void CTrayWnd::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == kTimerId) {
        Poll();
        return;
    }
    CWnd::OnTimer(nIDEvent);
}

// ---------------------------------------------------------------------------
// 서비스 상태

void CTrayWnd::Poll() {
    proto::Message resp;
    if (proto::Transact(proto::Message(proto::kStatus), resp, kPollTimeoutMs) && resp.IsOk())
        m_status = proto::ParseStatus(resp);
    else
        m_status = proto::Status();  // connected = false

    if (!m_iconAdded) AddTrayIcon();  // 로그온 직후 탐색기가 준비되지 않았을 수 있음
    UpdateUi();
}

CString CTrayWnd::StatusLine() const {
    if (!m_status.connected) return _T("서비스에 연결할 수 없습니다");
    if (!m_status.tracked) return _T("제한 대상 계정이 아닙니다");
    if (m_status.paused) return _T("오늘은 제한이 해제되었습니다");

    const cb::Status& s = m_status.engine;
    CString text;
    switch (s.phase) {
    case cb::Phase::Using:
    case cb::Phase::BreakWarning:
        if (s.msToBreak >= 0)
            text.Format(_T("다음 휴식까지 %s"), FormatDuration(s.msToBreak).GetString());
        else
            text = _T("휴식 없음");
        break;
    case cb::Phase::Break:
        text.Format(_T("쉬는 시간 %s 남음"), FormatDuration(s.msBreakRemaining).GetString());
        break;
    case cb::Phase::ShutdownWarning:
    case cb::Phase::Shutdown:
        text = _T("곧 종료됩니다");
        break;
    }
    return text;
}

CString CTrayWnd::ShutdownLine() const {
    if (!m_status.connected || !m_status.tracked) return _T("");
    if (m_status.paused || m_status.engine.msToShutdown < 0)
        return _T("오늘 사용 ") + FormatHoursMinutesLong(m_status.engine.usedTodayMs);
    CString text;
    text.Format(_T("종료까지 %s"), FormatDuration(m_status.engine.msToShutdown).GetString());
    return text;
}

void CTrayWnd::UpdateUi() {
    const bool active = m_status.connected && m_status.tracked && !m_status.paused;
    const cb::Status& s = m_status.engine;

    if (!m_status.connected) {
        UpdateTrayIcon(IconBreak);  // 회색
    } else if (!active) {
        UpdateTrayIcon(IconUsing);
    } else {
        switch (s.phase) {
        case cb::Phase::Using: UpdateTrayIcon(IconUsing); break;
        case cb::Phase::BreakWarning: UpdateTrayIcon(IconBreakSoon); break;
        case cb::Phase::Break: UpdateTrayIcon(IconBreak); break;
        default: UpdateTrayIcon(IconShutdown); break;
        }
    }
    UpdateTooltip();

    if (active && s.phase == cb::Phase::BreakWarning) {
        m_warning.ShowCountdown(_T("곧 쉬는 시간이에요"), s.msToBreak, m_status.breakWarnTotalMs,
                                _T("하던 일을 정리해 주세요"), kColorBreakSoon);
    } else if (active && s.phase == cb::Phase::ShutdownWarning) {
        m_warning.ShowCountdown(_T("컴퓨터가 곧 꺼져요"), s.msToShutdown, m_status.shutdownWarnTotalMs,
                                _T("작업을 저장해 주세요"), kColorShutdown);
    } else if (active && s.phase == cb::Phase::Shutdown && m_status.dryRun) {
        m_warning.ShowCountdown(_T("테스트 모드 · 종료 시점"), 0, 1, _T("시간 연장으로 계속할 수 있어요"), kColorShutdown);
    } else {
        m_warning.HideWnd();
    }

    if (active && s.phase == cb::Phase::Break)
        m_overlay.ShowBreak(s.msBreakRemaining, m_status.breakTotalMs);
    else
        m_overlay.HideWnd();
}

void CTrayWnd::UpdateTrayIcon(IconKind kind) {
    if (kind == m_currentIcon || !m_iconAdded) return;
    m_currentIcon = kind;
    m_nid.uFlags = NIF_ICON;
    m_nid.hIcon = m_icons[kind];
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

void CTrayWnd::UpdateTooltip() {
    if (!m_iconAdded) return;
    CString tip = kAppTitle;
    if (m_status.dryRun) tip += _T(" [테스트]");
    tip += _T("\n") + StatusLine();
    const CString second = ShutdownLine();
    if (!second.IsEmpty()) tip += _T("\n") + second;
    m_nid.uFlags = NIF_TIP;
    _tcsncpy_s(m_nid.szTip, tip, _TRUNCATE);
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

bool CTrayWnd::AddTrayIcon() {
    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = kTrayId;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_APP_TRAY;
    m_currentIcon = IconUsing;
    m_nid.hIcon = m_icons[IconUsing];
    _tcscpy_s(m_nid.szTip, kAppTitle);
    m_iconAdded = Shell_NotifyIcon(NIM_ADD, &m_nid) != FALSE;
    return m_iconAdded;
}

LRESULT CTrayWnd::OnTaskbarCreated(WPARAM, LPARAM) {
    // 탐색기가 다시 시작되면 아이콘을 다시 등록해야 한다.
    m_iconAdded = false;
    AddTrayIcon();
    UpdateUi();
    return 0;
}

void CTrayWnd::ShowBalloon(const CString& title, const CString& text) {
    if (!m_iconAdded) return;
    m_nid.uFlags = NIF_INFO;
    _tcsncpy_s(m_nid.szInfoTitle, title, _TRUNCATE);
    _tcsncpy_s(m_nid.szInfo, text, _TRUNCATE);
    m_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

// ---------------------------------------------------------------------------
// 트레이 메뉴

LRESULT CTrayWnd::OnTrayNotify(WPARAM, LPARAM lParam) {
    switch (static_cast<UINT>(lParam)) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;
    case WM_LBUTTONDBLCLK: {
        CString text = StatusLine();
        const CString second = ShutdownLine();
        if (!second.IsEmpty()) text += _T("\n") + second;
        ShowBalloon(kAppTitle, text);
        break;
    }
    }
    return 0;
}

void CTrayWnd::ShowContextMenu() {
    if (m_busy) return;

    const bool connected = m_status.connected;
    const bool tracked = connected && m_status.tracked;
    const bool inBreak = tracked && !m_status.paused &&
                         (m_status.engine.phase == cb::Phase::Break ||
                          m_status.engine.phase == cb::Phase::BreakWarning);
    const UINT needService = connected && m_status.hasPassword ? 0 : MF_GRAYED;
    const UINT needChild = tracked && m_status.hasPassword ? 0 : MF_GRAYED;

    CMenu extendMenu;
    extendMenu.CreatePopupMenu();
    extendMenu.AppendMenu(MF_STRING, ID_TRAY_EXTEND15, _T("15분"));
    extendMenu.AppendMenu(MF_STRING, ID_TRAY_EXTEND30, _T("30분"));
    extendMenu.AppendMenu(MF_STRING, ID_TRAY_EXTEND60, _T("1시간"));

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING | MF_GRAYED, ID_TRAY_STATUS, StatusLine());
    const CString second = ShutdownLine();
    if (!second.IsEmpty()) menu.AppendMenu(MF_STRING | MF_GRAYED, ID_TRAY_STATUS2, second);
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (connected ? 0 : MF_GRAYED), ID_TRAY_HISTORY, _T("사용 기록..."));
    menu.AppendMenu(MF_STRING | needService, ID_TRAY_SETTINGS, _T("설정...  🔒"));
    menu.AppendMenu(MF_STRING | (inBreak ? needChild : MF_GRAYED), ID_TRAY_SKIPBREAK, _T("휴식 건너뛰기  🔒"));
    menu.AppendMenu(MF_POPUP | needChild, reinterpret_cast<UINT_PTR>(extendMenu.m_hMenu), _T("시간 연장  🔒"));
    menu.AppendMenu(MF_STRING | needChild, ID_TRAY_PAUSE,
                    m_status.paused ? _T("제한 다시 켜기  🔒") : _T("오늘 제한 해제  🔒"));
    menu.AppendMenu(MF_STRING | needService, ID_TRAY_CHANGEPW, _T("비밀번호 변경...  🔒"));
    if (!tracked) {
        // 제한 대상이 아니거나 서비스가 없을 때만 트레이를 닫을 수 있다.
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, ID_TRAY_CLOSE, _T("트레이 닫기"));
    }

    CPoint pt;
    GetCursorPos(&pt);
    SetForegroundWindow();
    const UINT cmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, this);
    PostMessage(WM_NULL);
    extendMenu.Detach();  // menu 가 파괴될 때 하위 메뉴도 함께 파괴된다

    m_busy = true;
    switch (cmd) {
    case ID_TRAY_HISTORY: OnHistory(); break;
    case ID_TRAY_SETTINGS: OnSettings(); break;
    case ID_TRAY_SKIPBREAK: OnSkipBreak(nullptr); break;
    case ID_TRAY_EXTEND15: OnExtend(15); break;
    case ID_TRAY_EXTEND30: OnExtend(30); break;
    case ID_TRAY_EXTEND60: OnExtend(60); break;
    case ID_TRAY_PAUSE: OnPauseToggle(); break;
    case ID_TRAY_CHANGEPW: OnChangePassword(); break;
    case ID_TRAY_CLOSE: OnCloseTray(); break;
    }
    m_busy = false;
    Poll();
}

LRESULT CTrayWnd::OnUnlockRequest(WPARAM, LPARAM) {
    if (m_busy) return 0;
    m_busy = true;
    OnSkipBreak(&m_overlay);
    m_busy = false;
    Poll();
    return 0;
}

void CTrayWnd::OnHistory() { CHistoryDlg::Show(); }

void CTrayWnd::OnSettings() {
    CString pw;
    if (!AskPassword(nullptr, _T("설정을 변경하려면 비밀번호를 입력하세요."), pw)) return;
    proto::Message verify(proto::kVerify);
    verify.Set(_T("pw"), pw);
    proto::Message config;
    if (!CallService(nullptr, verify) || !CallService(nullptr, proto::Message(proto::kConfig), &config)) return;

    CSettingsDlg dlg(proto::ConfigFromMessage(config), m_status.tracked ? m_status.engine.usedTodayMs : -1);
    if (dlg.DoModal() != IDOK) return;

    proto::Message set(proto::kSetConfig);
    set.Set(_T("pw"), pw);
    proto::ConfigToMessage(dlg.m_cfg, set);
    if (CallService(nullptr, set)) ShowBalloon(kAppTitle, _T("설정이 저장되었습니다."));
}

void CTrayWnd::OnSkipBreak(CWnd* owner) {
    if (CallProtected(owner, _T("휴식을 끝내려면 부모 비밀번호를 입력하세요."), proto::Message(proto::kSkipBreak)))
        ShowBalloon(kAppTitle, _T("휴식을 건너뛰었습니다."));
}

void CTrayWnd::OnExtend(int minutes) {
    CString prompt;
    prompt.Format(_T("사용 시간을 %d분 연장하려면 비밀번호를 입력하세요."), minutes);
    proto::Message req(proto::kExtend);
    req.Set(_T("minutes"), static_cast<int64_t>(minutes));
    if (CallProtected(nullptr, prompt, req)) {
        CString text;
        text.Format(_T("%d분 연장되었습니다."), minutes);
        ShowBalloon(kAppTitle, text);
    }
}

void CTrayWnd::OnPauseToggle() {
    if (m_status.paused) {
        if (CallProtected(nullptr, _T("제한을 다시 켜려면 비밀번호를 입력하세요."), proto::Message(proto::kResume)))
            ShowBalloon(kAppTitle, _T("제한이 다시 켜졌습니다."));
    } else {
        if (CallProtected(nullptr, _T("오늘 하루 제한을 해제하려면 비밀번호를 입력하세요.\n(자정에 자동으로 다시 켜집니다)"),
                          proto::Message(proto::kPause)))
            ShowBalloon(kAppTitle, _T("오늘은 제한 없이 사용할 수 있습니다."));
    }
}

void CTrayWnd::OnChangePassword() {
    CString pw;
    if (!AskPassword(nullptr, _T("현재 비밀번호를 입력하세요."), pw)) return;
    proto::Message verify(proto::kVerify);
    verify.Set(_T("pw"), pw);
    if (!CallService(nullptr, verify)) return;

    CSetupPasswordDlg dlg(_T("새 비밀번호를 입력하세요."));
    if (dlg.DoModal() != IDOK) return;
    proto::Message req(proto::kSetPassword);
    req.Set(_T("pw"), pw);
    req.Set(_T("newpw"), dlg.m_password);
    if (CallService(nullptr, req)) ShowBalloon(kAppTitle, _T("비밀번호가 변경되었습니다."));
}

void CTrayWnd::OnCloseTray() {
    m_exitAuthorized = true;
    PostMessage(WM_CLOSE);  // 메뉴 처리 중에 자신을 파괴하지 않도록 지연
}
