#pragma once

// 휴식 중 모든 모니터를 덮는 전체 화면 창. 닫기/최소화를 막고, "부모 해제" 버튼만 제공한다.
class CBreakOverlayWnd : public CWnd {
public:
    BOOL CreateWnd(CWnd* pNotify);
    void ShowBreak(int64_t remainingMs, int64_t totalMs);
    void HideWnd();

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnClose();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
    afx_msg void OnUnlockClicked();
    DECLARE_MESSAGE_MAP()

    void Layout();

    CWnd* m_pNotify = nullptr;
    CButton m_unlockButton;
    CRect m_primary;  // 클라이언트 좌표 기준 주 모니터 영역
    CString m_remaining;
    double m_progress = 1.0;  // 남은 비율 (1 → 0)

    CFont m_timeFont;
    CFont m_labelFont;
    CFont m_buttonFont;
};
