#include "pch.h"
#include "SettingsDlg.h"

#include "Common.h"
#include "HistoryDlg.h"
#include "Settings.h"

BEGIN_MESSAGE_MAP(CSettingsDlg, CDialog)
    ON_BN_CLICKED(IDC_BREAK_ENABLE, &CSettingsDlg::OnToggle)
    ON_BN_CLICKED(IDC_LIMIT_ENABLE, &CSettingsDlg::OnToggle)
    ON_BN_CLICKED(IDC_SHUTAT_ENABLE, &CSettingsDlg::OnToggle)
    ON_BN_CLICKED(IDC_SHOW_HISTORY, &CSettingsDlg::OnShowHistory)
END_MESSAGE_MAP()

CSettingsDlg::CSettingsDlg(const cb::ScheduleConfig& cfg, int64_t usedTodayMs, CWnd* pParent)
    : CDialog(IDD_SETTINGS, pParent), m_cfg(cfg), m_usedTodayMs(usedTodayMs) {}

void CSettingsDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_SHUTAT_TIME, m_timeCtrl);
}

BOOL CSettingsDlg::OnInitDialog() {
    CDialog::OnInitDialog();
    ApplyAppIcon(this);

    CheckDlgButton(IDC_BREAK_ENABLE, m_cfg.breaksEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(IDC_USE_MIN, m_cfg.useMinutes, FALSE);
    SetDlgItemInt(IDC_BREAK_MIN, m_cfg.breakMinutes, FALSE);
    SetDlgItemInt(IDC_BREAK_WARN_SEC, m_cfg.breakWarnSeconds, FALSE);

    CheckDlgButton(IDC_LIMIT_ENABLE, m_cfg.dailyLimitEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(IDC_LIMIT_MIN, m_cfg.dailyLimitMinutes, FALSE);

    CheckDlgButton(IDC_SHUTAT_ENABLE, m_cfg.shutdownAtEnabled ? BST_CHECKED : BST_UNCHECKED);
    m_timeCtrl.SetFormat(_T("HH:mm"));
    SYSTEMTIME st;
    GetLocalTime(&st);
    st.wHour = static_cast<WORD>(m_cfg.shutdownAtMinuteOfDay / 60);
    st.wMinute = static_cast<WORD>(m_cfg.shutdownAtMinuteOfDay % 60);
    st.wSecond = 0;
    st.wMilliseconds = 0;
    m_timeCtrl.SetTime(&st);

    SetDlgItemInt(IDC_SHUT_WARN_MIN, m_cfg.shutdownWarnSeconds / 60, FALSE);
    CheckDlgButton(IDC_COUNT_BREAK, m_cfg.countBreakAsUsage ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemText(IDC_USED_TODAY, _T("오늘 사용한 시간: ") + FormatDuration(m_usedTodayMs));

    UpdateControls();
    SetForegroundWindow();
    return TRUE;
}

void CSettingsDlg::OnToggle() { UpdateControls(); }

void CSettingsDlg::OnShowHistory() { CHistoryDlg::Show(m_cfg, nullptr, this); }

void CSettingsDlg::UpdateControls() {
    const BOOL breaks = IsDlgButtonChecked(IDC_BREAK_ENABLE) == BST_CHECKED;
    GetDlgItem(IDC_USE_MIN)->EnableWindow(breaks);
    GetDlgItem(IDC_BREAK_MIN)->EnableWindow(breaks);
    GetDlgItem(IDC_BREAK_WARN_SEC)->EnableWindow(breaks);
    GetDlgItem(IDC_COUNT_BREAK)->EnableWindow(breaks);

    GetDlgItem(IDC_LIMIT_MIN)->EnableWindow(IsDlgButtonChecked(IDC_LIMIT_ENABLE) == BST_CHECKED);
    GetDlgItem(IDC_SHUTAT_TIME)->EnableWindow(IsDlgButtonChecked(IDC_SHUTAT_ENABLE) == BST_CHECKED);
}

bool CSettingsDlg::ReadField(UINT id, int minValue, int maxValue, LPCTSTR label, bool required, int& value) {
    BOOL translated = FALSE;
    const int v = static_cast<int>(GetDlgItemInt(id, &translated, FALSE));
    if (translated && v >= minValue && v <= maxValue) {
        value = v;
        return true;
    }
    if (!required) return true;  // 비활성 항목은 기존 값 유지

    CString msg;
    msg.Format(_T("%s 값은 %d ~ %d 사이로 입력하세요."), label, minValue, maxValue);
    MessageBox(msg, kAppTitle, MB_ICONWARNING);
    GotoDlgCtrl(GetDlgItem(id));
    return false;
}

void CSettingsDlg::OnOK() {
    using namespace Limits;
    cb::ScheduleConfig c = m_cfg;

    c.breaksEnabled = IsDlgButtonChecked(IDC_BREAK_ENABLE) == BST_CHECKED;
    c.dailyLimitEnabled = IsDlgButtonChecked(IDC_LIMIT_ENABLE) == BST_CHECKED;
    c.shutdownAtEnabled = IsDlgButtonChecked(IDC_SHUTAT_ENABLE) == BST_CHECKED;
    c.countBreakAsUsage = IsDlgButtonChecked(IDC_COUNT_BREAK) == BST_CHECKED;

    int shutWarnMin = c.shutdownWarnSeconds / 60;
    if (!ReadField(IDC_USE_MIN, kUseMinMin, kUseMinMax, _T("사용 시간"), c.breaksEnabled, c.useMinutes) ||
        !ReadField(IDC_BREAK_MIN, kBreakMinMin, kBreakMinMax, _T("휴식 시간"), c.breaksEnabled, c.breakMinutes) ||
        !ReadField(IDC_BREAK_WARN_SEC, kBreakWarnSecMin, kBreakWarnSecMax, _T("휴식 전 알림"), c.breaksEnabled,
                   c.breakWarnSeconds) ||
        !ReadField(IDC_LIMIT_MIN, kDailyLimitMinMin, kDailyLimitMinMax, _T("하루 사용 시간"), c.dailyLimitEnabled,
                   c.dailyLimitMinutes) ||
        !ReadField(IDC_SHUT_WARN_MIN, kShutWarnMinMin, kShutWarnMinMax, _T("종료 전 알림"), true, shutWarnMin)) {
        return;
    }
    c.shutdownWarnSeconds = shutWarnMin * 60;

    SYSTEMTIME st;
    if (m_timeCtrl.GetTime(&st) == GDT_VALID) c.shutdownAtMinuteOfDay = st.wHour * 60 + st.wMinute;

    if (c.breaksEnabled && c.breakWarnSeconds >= c.useMinutes * 60) {
        MessageBox(_T("휴식 전 알림 시간은 사용 시간보다 짧아야 합니다."), kAppTitle, MB_ICONWARNING);
        GotoDlgCtrl(GetDlgItem(IDC_BREAK_WARN_SEC));
        return;
    }

    m_cfg = c;
    CDialog::OnOK();
}
