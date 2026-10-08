#include "pch.h"
#include "PasswordDlg.h"

#include "Common.h"
#include "PasswordHasher.h"

// ---------------------------------------------------------------------------
// CPasswordDlg

CPasswordDlg::CPasswordDlg(const CString& prompt, CWnd* pParent)
    : CDialog(IDD_PASSWORD, pParent), m_prompt(prompt) {}

void CPasswordDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_PASSWORD, m_password);
}

BOOL CPasswordDlg::OnInitDialog() {
    CDialog::OnInitDialog();
    ApplyAppIcon(this);
    SetDlgItemText(IDC_PROMPT, m_prompt);
    SetForegroundWindow();
    GotoDlgCtrl(GetDlgItem(IDC_PASSWORD));
    return FALSE;
}

// ---------------------------------------------------------------------------
// CSetupPasswordDlg

CSetupPasswordDlg::CSetupPasswordDlg(const CString& prompt, CWnd* pParent)
    : CDialog(IDD_SETUP_PASSWORD, pParent), m_prompt(prompt) {}

void CSetupPasswordDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_PASSWORD, m_password);
    DDX_Text(pDX, IDC_PASSWORD2, m_confirm);
}

BOOL CSetupPasswordDlg::OnInitDialog() {
    CDialog::OnInitDialog();
    ApplyAppIcon(this);
    SetDlgItemText(IDC_PROMPT, m_prompt);
    SetForegroundWindow();
    GotoDlgCtrl(GetDlgItem(IDC_PASSWORD));
    return FALSE;
}

void CSetupPasswordDlg::OnOK() {
    if (!UpdateData(TRUE)) return;

    if (m_password.GetLength() < PasswordHasher::kMinLength) {
        CString msg;
        msg.Format(_T("비밀번호는 %d자 이상이어야 합니다."), PasswordHasher::kMinLength);
        MessageBox(msg, kAppTitle, MB_ICONWARNING);
        GotoDlgCtrl(GetDlgItem(IDC_PASSWORD));
        return;
    }
    if (m_password != m_confirm) {
        MessageBox(_T("두 비밀번호가 일치하지 않습니다."), kAppTitle, MB_ICONWARNING);
        SetDlgItemText(IDC_PASSWORD2, _T(""));
        GotoDlgCtrl(GetDlgItem(IDC_PASSWORD2));
        return;
    }
    CDialog::OnOK();
}
