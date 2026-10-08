#pragma once

#include "ScheduleEngine.h"
#include "resource.h"

class CSettingsDlg : public CDialog {
public:
    CSettingsDlg(const cb::ScheduleConfig& cfg, int64_t usedTodayMs, CWnd* pParent = nullptr);
    enum { IDD = IDD_SETTINGS };

    cb::ScheduleConfig m_cfg;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    afx_msg void OnToggle();
    afx_msg void OnShowHistory();
    DECLARE_MESSAGE_MAP()

    void UpdateControls();
    bool ReadField(UINT id, int minValue, int maxValue, LPCTSTR label, bool required, int& value);

    int64_t m_usedTodayMs;
    CDateTimeCtrl m_timeCtrl;
};
