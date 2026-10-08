#pragma once

#include "resource.h"

// 비밀번호 입력
class CPasswordDlg : public CDialog {
public:
    CPasswordDlg(const CString& prompt, CWnd* pParent = nullptr);
    enum { IDD = IDD_PASSWORD };

    CString m_password;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    CString m_prompt;
};

// 새 비밀번호 등록 (2회 입력)
class CSetupPasswordDlg : public CDialog {
public:
    CSetupPasswordDlg(const CString& prompt, CWnd* pParent = nullptr);
    enum { IDD = IDD_SETUP_PASSWORD };

    CString m_password;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    CString m_prompt;
    CString m_confirm;
};
