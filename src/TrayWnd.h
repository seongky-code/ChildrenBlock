#pragma once

#include "BreakOverlayWnd.h"
#include "ScheduleEngine.h"
#include "Settings.h"
#include "WarningWnd.h"

struct AppOptions {
    bool dryRun = false;  // 실제로 PC 를 끄지 않음 (테스트용)
    int speed = 1;        // 사용 시간 진행 배속 (테스트용)
};

// 보이지 않는 메인 창. 트레이 아이콘, 1초 타이머, 메뉴 명령을 담당한다.
class CTrayWnd : public CWnd {
public:
    CTrayWnd(AppSettings& settings, const AppOptions& options);
    BOOL CreateWnd();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnClose();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg BOOL OnQueryEndSession();
    afx_msg void OnEndSession(BOOL bEnding);
    afx_msg LRESULT OnTrayNotify(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTaskbarCreated(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnUnlockRequest(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnPowerBroadcast(WPARAM wParam, LPARAM lParam);
    virtual void PostNcDestroy();
    DECLARE_MESSAGE_MAP()

private:
    enum IconKind { IconUsing, IconBreakSoon, IconBreak, IconShutdown, IconCount };

    void Tick(bool advanceTime);
    void UpdateUi(const cb::Status& status);
    void UpdateTrayIcon(IconKind kind);
    void UpdateTooltip(const cb::Status& status);
    CString StatusLine(const cb::Status& status) const;
    CString ShutdownLine(const cb::Status& status) const;
    bool AddTrayIcon();
    void ShowBalloon(const CString& title, const CString& text);
    void ShowContextMenu();
    void DoShutdown();
    void SaveState();
    void ReloadSettingsIfChanged();

    bool Authenticate(CWnd* pParent, const CString& prompt);
    void OnHistory();
    void OnSettings();
    void OnSkipBreak();
    void OnExtend(int minutes);
    void OnChangePassword();
    void OnExit();

    static int TodayAsInt(const SYSTEMTIME& st);
    static int64_t SecondsOfDay(const SYSTEMTIME& st);

    AppSettings& m_settings;
    AppOptions m_options;
    cb::ScheduleEngine m_engine;

    NOTIFYICONDATA m_nid = {};
    bool m_iconAdded = false;
    HICON m_icons[IconCount] = {};
    IconKind m_currentIcon = IconCount;

    CWarningWnd m_warning;
    CBreakOverlayWnd m_overlay;

    ULONGLONG m_lastTick = 0;
    int m_today = 0;
    int m_ticksSinceSave = 0;
    int m_ticksSinceReloadCheck = 0;
    ULONGLONG m_settingsStamp = 0;

    bool m_exitAuthorized = false;  // 비밀번호 확인 후에만 WM_CLOSE 허용
    bool m_busy = false;  // 모달 대화 상자 중복 방지
    int m_failedAttempts = 0;
    ULONGLONG m_lockedUntil = 0;

    ULONGLONG m_shutdownIssuedAt = 0;
    bool m_shutdownForced = false;
};
