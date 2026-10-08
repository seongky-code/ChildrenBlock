#pragma once

#include "BreakOverlayWnd.h"
#include "Protocol.h"
#include "WarningWnd.h"

// 보이지 않는 메인 창. 1초마다 서비스에서 상태를 받아 트레이 아이콘·알림·휴식 화면을 그린다.
// 시간 계산·저장·PC 종료는 모두 서비스가 담당한다.
class CTrayWnd : public CWnd {
public:
    BOOL CreateWnd();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnClose();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg LRESULT OnTrayNotify(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnTaskbarCreated(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnUnlockRequest(WPARAM wParam, LPARAM lParam);
    virtual void PostNcDestroy();
    DECLARE_MESSAGE_MAP()

private:
    enum IconKind { IconUsing, IconBreakSoon, IconBreak, IconShutdown, IconCount };

    void Poll();
    void UpdateUi();
    void UpdateTrayIcon(IconKind kind);
    void UpdateTooltip();
    CString StatusLine() const;
    CString ShutdownLine() const;
    bool AddTrayIcon();
    void ShowBalloon(const CString& title, const CString& text);
    void ShowContextMenu();

    void OnHistory();
    void OnSettings();
    void OnSkipBreak(CWnd* owner);
    void OnExtend(int minutes);
    void OnPauseToggle();
    void OnChangePassword();
    void OnCloseTray();

    proto::Status m_status;

    NOTIFYICONDATA m_nid = {};
    bool m_iconAdded = false;
    HICON m_icons[IconCount] = {};
    IconKind m_currentIcon = IconCount;

    CWarningWnd m_warning;
    CBreakOverlayWnd m_overlay;

    bool m_busy = false;            // 모달 대화 상자 중복 방지
    bool m_exitAuthorized = false;  // WM_CLOSE 는 "트레이 닫기" 를 고른 경우에만 허용
};
