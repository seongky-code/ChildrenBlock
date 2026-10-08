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

class CHistoryDlg : public CDialog {
public:
    // records: 날짜별 기록 (순서 무관), dailyLimitMs: 하루 제한 (-1 이면 표시 안 함)
    CHistoryDlg(std::vector<UsageRecord> records, int64_t dailyLimitMs, CWnd* pParent = nullptr);
    enum { IDD = IDD_HISTORY };

    // 파일의 기록에 실행 중인 앱의 오늘 값을 덮어써 보여 준다 (live 가 nullptr 이면 파일 값만).
    static void Show(const cb::ScheduleConfig& cfg, const UsageRecord* live, CWnd* pParent = nullptr);

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    std::vector<UsageRecord> m_records;
    int64_t m_limitMs;
    CUsageChart m_chart;
    CListCtrl m_list;
};
