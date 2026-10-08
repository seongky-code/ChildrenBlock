#pragma once

#include "Settings.h"
#include "resource.h"

// 최근 N일 사용 시간 막대 그래프
class CUsageChart : public CStatic {
public:
    struct Day {
        int dayOfMonth = 0;
        int64_t usedMs = 0;
        bool today = false;
    };
    void SetData(const std::vector<Day>& days, int64_t limitMs);

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

    std::vector<Day> m_days;
    int64_t m_limitMs = -1;  // -1: 제한 없음
    CFont m_smallFont;
};

// 사용 기록 화면. 기록은 서비스에서 받아온다.
class CHistoryDlg : public CDialog {
public:
    explicit CHistoryDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_HISTORY };

    static void Show(CWnd* pParent = nullptr);

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    afx_msg void OnUserChanged();
    DECLARE_MESSAGE_MAP()

    bool Load(const CString& user);
    void Populate();

    CString m_user;
    std::vector<CString> m_children;
    std::vector<UsageRecord> m_records;
    int64_t m_limitMs = -1;

    CUsageChart m_chart;
    CListCtrl m_list;
    CComboBox m_userCombo;
};
