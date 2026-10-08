#pragma once

// 화면 오른쪽 아래에 뜨는 항상 위 카운트다운 카드. 포커스를 빼앗지 않는다.
class CWarningWnd : public CWnd {
public:
    BOOL CreateWnd();
    // remainingMs / totalMs 비율만큼 진행 막대를 채운다.
    void ShowCountdown(const CString& title, int64_t remainingMs, int64_t totalMs, const CString& hint,
                       COLORREF accent);
    void HideWnd();

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnClose();
    afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
    DECLARE_MESSAGE_MAP()

    CString m_title;
    CString m_time;
    CString m_hint;
    double m_progress = 1.0;
    COLORREF m_accent = RGB(0, 0, 0);

    CFont m_titleFont;
    CFont m_timeFont;
    CFont m_hintFont;
};
