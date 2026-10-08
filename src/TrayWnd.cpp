#include "pch.h"
#include "TrayWnd.h"

#include "Common.h"
#include "HistoryDlg.h"
#include "PasswordDlg.h"
#include "PasswordHasher.h"
#include "SettingsDlg.h"

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTrayId = 1;
constexpr int kSaveEveryTicks = 30;
constexpr int kReloadCheckTicks = 5;
constexpr int kMaxFailedAttempts = 5;
constexpr ULONGLONG kLockoutMs = 5 * 60 * 1000;
constexpr ULONGLONG kForceShutdownAfterMs = 60 * 1000;
constexpr int64_t kMaxTickDeltaMs = 5000;  // 이보다 길면 절전/정지로 보고 1초만 계산

enum : UINT {
    ID_TRAY_STATUS = 40001,
    ID_TRAY_STATUS2,
    ID_TRAY_HISTORY,
    ID_TRAY_SETTINGS,
    ID_TRAY_SKIPBREAK,
    ID_TRAY_EXTEND15,
    ID_TRAY_EXTEND30,
    ID_TRAY_EXTEND60,
    ID_TRAY_CHANGEPW,
    ID_TRAY_EXIT,
};

// 알림 강조색 (트레이 아이콘 색과 맞춤)
const COLORREF kColorBreakSoon = RGB(240, 176, 72);
const COLORREF kColorShutdown = RGB(236, 98, 98);

const UINT s_msgTaskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));

LPCTSTR PhaseName(cb::Phase p) {
    switch (p) {
    case cb::Phase::Using: return _T("사용");
    case cb::Phase::BreakWarning: return _T("휴식 임박");
    case cb::Phase::Break: return _T("휴식");
    case cb::Phase::ShutdownWarning: return _T("종료 임박");
    case cb::Phase::Shutdown: return _T("종료");
    }
    return _T("?");
}

}  // namespace

BEGIN_MESSAGE_MAP(CTrayWnd, CWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_CLOSE()
    ON_WM_TIMER()
    ON_WM_QUERYENDSESSION()
    ON_WM_ENDSESSION()
    ON_MESSAGE(WM_APP_TRAY, &CTrayWnd::OnTrayNotify)
    ON_MESSAGE(WM_APP_UNLOCK_REQUEST, &CTrayWnd::OnUnlockRequest)
    ON_MESSAGE(WM_POWERBROADCAST, &CTrayWnd::OnPowerBroadcast)
    ON_REGISTERED_MESSAGE(s_msgTaskbarCreated, &CTrayWnd::OnTaskbarCreated)
END_MESSAGE_MAP()

CTrayWnd::CTrayWnd(AppSettings& settings, const AppOptions& options)
    : m_settings(settings), m_options(options), m_engine(settings.schedule) {}

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

    SYSTEMTIME st;
    GetLocalTime(&st);
    m_today = TodayAsInt(st);

    SavedState saved;
    cb::PersistentState restore;
    if (SettingsStore::LoadState(saved) && saved.date == m_today) restore = saved.engine;
    m_engine.Restore(restore, SecondsOfDay(st));

    LogEvent(_T("프로그램 시작 (오늘 사용 %s)%s"), FormatDuration(restore.usedTodayMs).GetString(),
             m_options.dryRun ? _T(" [테스트 모드]") : _T(""));

    SettingsStore::PruneHistory(kHistoryKeepDays);
    m_settingsStamp = SettingsStore::SettingsFileStamp();
    AddTrayIcon();
    m_lastTick = GetTickCount64();
    SetTimer(kTimerId, 1000, nullptr);
    Tick(false);
    SaveState();  // 오늘 날짜를 사용 기록에 바로 남김
    return 0;
}

void CTrayWnd::OnDestroy() {
    KillTimer(kTimerId);
    SaveState();
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
    if (!m_exitAuthorized) {
        LogEvent(_T("외부 종료 요청(WM_CLOSE) 무시"));
        return;
    }
    DestroyWindow();  // 메인 창이 파괴되면 메시지 루프가 끝난다
}

BOOL CTrayWnd::OnQueryEndSession() {
    SaveState();
    return TRUE;
}

void CTrayWnd::OnEndSession(BOOL bEnding) {
    if (bEnding) {
        SaveState();
        LogEvent(_T("Windows 종료/로그오프"));
    }
}

LRESULT CTrayWnd::OnPowerBroadcast(WPARAM wParam, LPARAM) {
    if (wParam == PBT_APMSUSPEND) {
        SaveState();
        LogEvent(_T("절전 모드 진입"));
    } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
        m_lastTick = GetTickCount64();  // 절전 시간은 사용 시간에 넣지 않는다
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// 타이머

int CTrayWnd::TodayAsInt(const SYSTEMTIME& st) { return st.wYear * 10000 + st.wMonth * 100 + st.wDay; }

int64_t CTrayWnd::SecondsOfDay(const SYSTEMTIME& st) { return st.wHour * 3600 + st.wMinute * 60 + st.wSecond; }

void CTrayWnd::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == kTimerId) {
        Tick(true);
        return;
    }
    CWnd::OnTimer(nIDEvent);
}

void CTrayWnd::Tick(bool advanceTime) {
    const ULONGLONG now = GetTickCount64();
    int64_t delta = static_cast<int64_t>(now - m_lastTick);
    m_lastTick = now;
    if (!advanceTime) delta = 0;
    if (delta > kMaxTickDeltaMs) delta = 1000;
    delta *= m_options.speed;

    SYSTEMTIME st;
    GetLocalTime(&st);
    const int today = TodayAsInt(st);
    if (today != m_today) {
        SaveState();  // 어제 기록을 마지막 값으로 확정
        m_engine.ResetDay();
        m_today = today;
        LogEvent(_T("날짜 변경 - 오늘 사용 시간 초기화"));
        SaveState();
    }

    const cb::Phase before = m_engine.GetPhase();
    m_engine.Advance(delta, SecondsOfDay(st));
    const cb::Status status = m_engine.GetStatus();

    if (status.phase != before) {
        LogEvent(_T("상태 변경: %s → %s (오늘 사용 %s)"), PhaseName(before), PhaseName(status.phase),
                 FormatDuration(status.usedTodayMs).GetString());
        SaveState();
    } else if (++m_ticksSinceSave >= kSaveEveryTicks) {
        SaveState();
    }

    if (++m_ticksSinceReloadCheck >= kReloadCheckTicks) ReloadSettingsIfChanged();
    if (!m_iconAdded) AddTrayIcon();  // 로그온 직후 탐색기가 준비되지 않았을 수 있음
    UpdateUi(status);

    if (status.phase == cb::Phase::Shutdown) {
        DoShutdown();
    } else {
        m_shutdownIssuedAt = 0;
        m_shutdownForced = false;
    }
}

void CTrayWnd::SaveState() {
    SavedState st;
    st.date = m_today;
    st.engine = m_engine.Save();
    SettingsStore::SaveState(st);
    m_ticksSinceSave = 0;
}

// 부모 계정에서 /setup 으로 바꾼 설정을 실행 중인 앱에 반영한다.
void CTrayWnd::ReloadSettingsIfChanged() {
    m_ticksSinceReloadCheck = 0;
    const ULONGLONG stamp = SettingsStore::SettingsFileStamp();
    if (stamp == 0 || stamp == m_settingsStamp) return;
    m_settingsStamp = stamp;

    AppSettings loaded;
    SettingsStore::Load(loaded);
    if (!loaded.HasPassword()) return;  // 쓰는 중이거나 손상된 파일은 무시
    m_settings = loaded;
    m_engine.SetConfig(m_settings.schedule);
    LogEvent(_T("설정 파일 변경 감지 - 다시 읽음"));
}

// ---------------------------------------------------------------------------
// 화면 갱신

CString CTrayWnd::StatusLine(const cb::Status& s) const {
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

CString CTrayWnd::ShutdownLine(const cb::Status& s) const {
    if (s.msToShutdown < 0) return _T("종료 제한 없음");
    CString text;
    text.Format(_T("종료까지 %s"), FormatDuration(s.msToShutdown).GetString());
    return text;
}

void CTrayWnd::UpdateUi(const cb::Status& s) {
    switch (s.phase) {
    case cb::Phase::Using: UpdateTrayIcon(IconUsing); break;
    case cb::Phase::BreakWarning: UpdateTrayIcon(IconBreakSoon); break;
    case cb::Phase::Break: UpdateTrayIcon(IconBreak); break;
    default: UpdateTrayIcon(IconShutdown); break;
    }
    UpdateTooltip(s);

    const cb::ScheduleConfig& cfg = m_settings.schedule;
    if (s.phase == cb::Phase::BreakWarning) {
        m_warning.ShowCountdown(_T("곧 쉬는 시간이에요"), s.msToBreak, cfg.breakWarnSeconds * 1000LL,
                                _T("하던 일을 정리해 주세요"), kColorBreakSoon);
    } else if (s.phase == cb::Phase::ShutdownWarning) {
        m_warning.ShowCountdown(_T("컴퓨터가 곧 꺼져요"), s.msToShutdown, cfg.shutdownWarnSeconds * 1000LL,
                                _T("작업을 저장해 주세요"), kColorShutdown);
    } else if (s.phase == cb::Phase::Shutdown && m_options.dryRun) {
        m_warning.ShowCountdown(_T("테스트 모드 · 종료 시점"), 0, 1, _T("시간 연장으로 계속할 수 있어요"), kColorShutdown);
    } else {
        m_warning.HideWnd();
    }

    if (s.phase == cb::Phase::Break)
        m_overlay.ShowBreak(s.msBreakRemaining, static_cast<int64_t>(m_settings.schedule.breakMinutes) * 60 * 1000);
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

void CTrayWnd::UpdateTooltip(const cb::Status& s) {
    if (!m_iconAdded) return;
    CString tip;
    tip.Format(_T("%s%s\n%s\n%s"), kAppTitle, m_options.dryRun ? _T(" [테스트]") : _T(""), StatusLine(s).GetString(),
               ShutdownLine(s).GetString());
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
    Tick(false);
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
        const cb::Status s = m_engine.GetStatus();
        ShowBalloon(kAppTitle, StatusLine(s) + _T("\n") + ShutdownLine(s) + _T("\n오늘 사용: ") +
                                   FormatDuration(s.usedTodayMs));
        break;
    }
    }
    return 0;
}

void CTrayWnd::ShowContextMenu() {
    if (m_busy) return;

    const cb::Status s = m_engine.GetStatus();
    const bool inBreak = s.phase == cb::Phase::Break || s.phase == cb::Phase::BreakWarning;

    CMenu extendMenu;
    extendMenu.CreatePopupMenu();
    extendMenu.AppendMenu(MF_STRING, ID_TRAY_EXTEND15, _T("15분"));
    extendMenu.AppendMenu(MF_STRING, ID_TRAY_EXTEND30, _T("30분"));
    extendMenu.AppendMenu(MF_STRING, ID_TRAY_EXTEND60, _T("1시간"));

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING | MF_GRAYED, ID_TRAY_STATUS, StatusLine(s));
    menu.AppendMenu(MF_STRING | MF_GRAYED, ID_TRAY_STATUS2, ShutdownLine(s));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, ID_TRAY_HISTORY, _T("사용 기록..."));
    menu.AppendMenu(MF_STRING, ID_TRAY_SETTINGS, _T("설정...  🔒"));
    menu.AppendMenu(MF_STRING | (inBreak ? 0 : MF_GRAYED), ID_TRAY_SKIPBREAK, _T("휴식 건너뛰기  🔒"));
    menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(extendMenu.m_hMenu), _T("시간 연장  🔒"));
    menu.AppendMenu(MF_STRING, ID_TRAY_CHANGEPW, _T("비밀번호 변경...  🔒"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, ID_TRAY_EXIT, _T("프로그램 종료  🔒"));

    CPoint pt;
    GetCursorPos(&pt);
    SetForegroundWindow();
    const UINT cmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, this);
    PostMessage(WM_NULL);
    extendMenu.Detach();  // menu 가 파괴될 때 하위 메뉴도 함께 파괴된다

    switch (cmd) {
    case ID_TRAY_HISTORY: OnHistory(); break;
    case ID_TRAY_SETTINGS: OnSettings(); break;
    case ID_TRAY_SKIPBREAK: OnSkipBreak(); break;
    case ID_TRAY_EXTEND15: OnExtend(15); break;
    case ID_TRAY_EXTEND30: OnExtend(30); break;
    case ID_TRAY_EXTEND60: OnExtend(60); break;
    case ID_TRAY_CHANGEPW: OnChangePassword(); break;
    case ID_TRAY_EXIT: OnExit(); break;
    }
}

bool CTrayWnd::Authenticate(CWnd* pParent, const CString& prompt) {
    const HWND owner = pParent ? pParent->GetSafeHwnd() : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (now < m_lockedUntil) {
        CString msg;
        msg.Format(_T("비밀번호를 여러 번 틀려 잠겨 있습니다.\n%s 후에 다시 시도하세요."),
                   FormatDuration(static_cast<int64_t>(m_lockedUntil - now)).GetString());
        ::MessageBox(owner, msg, kAppTitle, MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
        return false;
    }

    CPasswordDlg dlg(prompt, pParent);
    if (dlg.DoModal() != IDOK) return false;

    if (PasswordHasher::Verify(dlg.m_password, m_settings.passwordSalt, m_settings.passwordHash,
                               m_settings.passwordIterations)) {
        m_failedAttempts = 0;
        return true;
    }

    ++m_failedAttempts;
    LogEvent(_T("비밀번호 틀림 (%d회)"), m_failedAttempts);
    if (m_failedAttempts >= kMaxFailedAttempts) {
        m_failedAttempts = 0;
        m_lockedUntil = GetTickCount64() + kLockoutMs;
        LogEvent(_T("비밀번호 입력 5분 잠금"));
        ::MessageBox(owner, _T("비밀번호를 5회 틀렸습니다.\n5분 동안 입력할 수 없습니다."), kAppTitle,
                     MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
    } else {
        ::MessageBox(owner, _T("비밀번호가 올바르지 않습니다."), kAppTitle,
                     MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
    }
    return false;
}

LRESULT CTrayWnd::OnUnlockRequest(WPARAM, LPARAM) {
    if (m_busy || m_engine.GetPhase() != cb::Phase::Break) return 0;
    m_busy = true;
    if (Authenticate(&m_overlay, _T("휴식을 끝내려면 부모 비밀번호를 입력하세요."))) {
        m_engine.SkipBreak();
        LogEvent(_T("부모가 휴식 해제"));
        Tick(false);
    }
    m_busy = false;
    return 0;
}

void CTrayWnd::OnHistory() {
    m_busy = true;
    SaveState();
    const cb::PersistentState st = m_engine.Save();
    UsageRecord live;
    live.date = m_today;
    live.usedMs = st.usedTodayMs;
    live.bonusMs = st.bonusMs;
    CHistoryDlg::Show(m_settings.schedule, &live);
    m_busy = false;
}

void CTrayWnd::OnSettings() {
    m_busy = true;
    if (Authenticate(nullptr, _T("설정을 변경하려면 비밀번호를 입력하세요."))) {
        CSettingsDlg dlg(m_settings.schedule, m_engine.GetStatus().usedTodayMs);
        if (dlg.DoModal() == IDOK) {
            m_settings.schedule = dlg.m_cfg;
            if (SettingsStore::Save(m_settings)) {
                m_settingsStamp = SettingsStore::SettingsFileStamp();
            } else {
                // 설치 시 settings.ini 는 자녀 계정에서 읽기 전용이다.
                ::MessageBox(nullptr,
                             _T("이 계정에서는 설정 파일을 저장할 수 없어 이번 로그인 동안만 적용됩니다.\n")
                             _T("계속 유지하려면 부모 계정에서 'ChildrenBlock 설정'을 실행하세요."),
                             kAppTitle, MB_ICONINFORMATION | MB_TOPMOST);
            }
            m_engine.SetConfig(m_settings.schedule);
            LogEvent(_T("설정 변경: 사용 %d분/휴식 %d분(%s), 하루 %d분(%s), 종료 %02d:%02d(%s)"),
                     m_settings.schedule.useMinutes, m_settings.schedule.breakMinutes,
                     m_settings.schedule.breaksEnabled ? _T("켜짐") : _T("꺼짐"),
                     m_settings.schedule.dailyLimitMinutes,
                     m_settings.schedule.dailyLimitEnabled ? _T("켜짐") : _T("꺼짐"),
                     m_settings.schedule.shutdownAtMinuteOfDay / 60, m_settings.schedule.shutdownAtMinuteOfDay % 60,
                     m_settings.schedule.shutdownAtEnabled ? _T("켜짐") : _T("꺼짐"));
            Tick(false);
        }
    }
    m_busy = false;
}

void CTrayWnd::OnSkipBreak() {
    m_busy = true;
    if (Authenticate(nullptr, _T("휴식을 건너뛰려면 비밀번호를 입력하세요."))) {
        m_engine.SkipBreak();
        LogEvent(_T("휴식 건너뛰기"));
        Tick(false);
    }
    m_busy = false;
}

void CTrayWnd::OnExtend(int minutes) {
    m_busy = true;
    CString prompt;
    prompt.Format(_T("사용 시간을 %d분 연장하려면 비밀번호를 입력하세요."), minutes);
    if (Authenticate(nullptr, prompt)) {
        m_engine.Extend(static_cast<int64_t>(minutes) * 60 * 1000);
        LogEvent(_T("사용 시간 %d분 연장"), minutes);
        Tick(false);
        CString text;
        text.Format(_T("%d분 연장되었습니다."), minutes);
        ShowBalloon(kAppTitle, text);
    }
    m_busy = false;
}

void CTrayWnd::OnChangePassword() {
    m_busy = true;
    if (Authenticate(nullptr, _T("현재 비밀번호를 입력하세요."))) {
        CSetupPasswordDlg dlg(_T("새 비밀번호를 입력하세요."));
        if (dlg.DoModal() == IDOK) {
            AppSettings updated = m_settings;
            if (PasswordHasher::Create(dlg.m_password, updated.passwordSalt, updated.passwordHash,
                                       updated.passwordIterations) &&
                SettingsStore::Save(updated)) {
                m_settings = updated;
                m_settingsStamp = SettingsStore::SettingsFileStamp();
                LogEvent(_T("비밀번호 변경"));
                ShowBalloon(kAppTitle, _T("비밀번호가 변경되었습니다."));
            } else {
                ::MessageBox(nullptr,
                             _T("이 계정에서는 비밀번호를 바꿀 수 없습니다.\n")
                             _T("부모 계정에서 'ChildrenBlock 설정'을 실행하세요."),
                             kAppTitle, MB_ICONWARNING | MB_TOPMOST);
            }
        }
    }
    m_busy = false;
}

void CTrayWnd::OnExit() {
    m_busy = true;
    if (Authenticate(nullptr, _T("프로그램을 종료하려면 비밀번호를 입력하세요."))) {
        LogEvent(_T("프로그램 종료 (부모)"));
        m_exitAuthorized = true;
        PostMessage(WM_CLOSE);  // 메뉴 처리 중에 자신을 파괴하지 않도록 지연
    }
    m_busy = false;
}

// ---------------------------------------------------------------------------
// PC 종료

void CTrayWnd::DoShutdown() {
    const ULONGLONG now = GetTickCount64();
    if (m_shutdownIssuedAt != 0 && (m_shutdownForced || now - m_shutdownIssuedAt < kForceShutdownAfterMs)) return;

    // 일반 종료가 저장 안 한 문서 등으로 취소되면 1분 뒤 강제 종료한다.
    const bool force = m_shutdownIssuedAt != 0;
    m_shutdownIssuedAt = now;
    m_shutdownForced = force;
    SaveState();

    if (m_options.dryRun) {
        LogEvent(_T("[테스트 모드] PC 종료 생략%s"), force ? _T(" (강제)") : _T(""));
        return;
    }

    LogEvent(_T("PC 종료 실행%s"), force ? _T(" (강제)") : _T(""));

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        TOKEN_PRIVILEGES tp = {};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValue(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(token);
    }

    const UINT flags = EWX_SHUTDOWN | (force ? EWX_FORCE : EWX_FORCEIFHUNG);
    if (!ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED)) {
        LogEvent(_T("ExitWindowsEx 실패 (%lu) - shutdown.exe 사용"), GetLastError());
        ShellExecute(nullptr, _T("open"), _T("shutdown.exe"), force ? _T("/s /f /t 0") : _T("/s /t 0"), nullptr,
                     SW_HIDE);
    }
}
