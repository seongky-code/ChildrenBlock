#include "pch.h"
#include "UiUtil.h"

#include "Common.h"
#include "PasswordDlg.h"
#include "resource.h"

int ScaleForDpi(int value) {
    static int dpi = 0;
    if (dpi == 0) {
        HDC hdc = ::GetDC(nullptr);
        dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ::ReleaseDC(nullptr, hdc);
        if (dpi <= 0) dpi = 96;
    }
    return MulDiv(value, dpi, 96);
}

void ApplyAppIcon(CWnd* wnd) {
    const HINSTANCE res = AfxGetResourceHandle();
    auto load = [res](int cx, int cy) {
        return static_cast<HICON>(LoadImage(res, MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON, cx, cy, LR_SHARED));
    };
    wnd->SetIcon(load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)), TRUE);
    wnd->SetIcon(load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)), FALSE);
}

int ShowMessage(CWnd* owner, LPCTSTR text, UINT flags) {
    return ::MessageBox(owner ? owner->GetSafeHwnd() : nullptr, text, kAppTitle, flags | MB_TOPMOST | MB_SETFOREGROUND);
}

bool CallService(CWnd* owner, const proto::Message& request, proto::Message* response) {
    proto::Message resp;
    if (!proto::Transact(request, resp, 10000)) {
        ShowMessage(owner, _T("ChildrenBlock 서비스에 연결할 수 없습니다.\n서비스가 실행 중인지 확인하세요."), MB_ICONERROR);
        return false;
    }
    if (resp.IsOk()) {
        if (response) *response = resp;
        return true;
    }

    const CString code = resp.Get(_T("code"));
    CString text;
    if (code == proto::kErrBadPassword) {
        text = _T("비밀번호가 올바르지 않습니다.");
    } else if (code == proto::kErrLocked) {
        text.Format(_T("비밀번호를 여러 번 틀려 잠겨 있습니다.\n%s 후에 다시 시도하세요."),
                    FormatDuration(resp.GetInt(_T("lockedMs"))).GetString());
    } else if (code == proto::kErrInvalid) {
        text = _T("입력 값이 올바르지 않습니다.");
    } else {
        text = resp.Get(_T("message"), _T("요청을 처리하지 못했습니다."));
    }
    ShowMessage(owner, text, MB_ICONWARNING);
    return false;
}

bool AskPassword(CWnd* owner, const CString& prompt, CString& password) {
    CPasswordDlg dlg(prompt, owner);
    if (dlg.DoModal() != IDOK) return false;
    password = dlg.m_password;
    return true;
}

bool CallProtected(CWnd* owner, const CString& prompt, proto::Message request, proto::Message* response) {
    CString pw;
    if (!AskPassword(owner, prompt, pw)) return false;
    request.Set(_T("pw"), pw);
    return CallService(owner, request, response);
}
