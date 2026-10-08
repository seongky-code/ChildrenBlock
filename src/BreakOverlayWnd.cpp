#include "pch.h"
#include "BreakOverlayWnd.h"

#include "Common.h"
#include "resource.h"

namespace {

const COLORREF kBackground = RGB(22, 24, 28);
const COLORREF kText = RGB(236, 238, 241);
const COLORREF kMuted = RGB(128, 134, 145);
const COLORREF kTrack = RGB(48, 52, 59);
const COLORREF kAccent = RGB(122, 162, 230);

void CreateUiFont(CFont& font, LPCTSTR face, int pointSize10x, int weight) {
    LOGFONT lf = {};
    lf.lfHeight = pointSize10x;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    _tcscpy_s(lf.lfFaceName, face);
    font.CreatePointFontIndirect(&lf);
}

}  // namespace

BEGIN_MESSAGE_MAP(CBreakOverlayWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CLOSE()
    ON_WM_SYSCOMMAND()
    ON_WM_DRAWITEM()
    ON_BN_CLICKED(IDC_UNLOCK_BUTTON, &CBreakOverlayWnd::OnUnlockClicked)
END_MESSAGE_MAP()

BOOL CBreakOverlayWnd::CreateWnd(CWnd* pNotify) {
    m_pNotify = pNotify;
    const CString cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW));
    if (!CreateEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, cls, _T("ChildrenBlock 휴식"), WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                  10, 10, nullptr, 0)) {
        return FALSE;
    }

    CreateUiFont(m_timeFont, _T("맑은 고딕 Semilight"), 1200, FW_LIGHT);
    CreateUiFont(m_labelFont, _T("맑은 고딕"), 160, FW_NORMAL);
    CreateUiFont(m_buttonFont, _T("맑은 고딕"), 95, FW_NORMAL);

    m_unlockButton.Create(_T("부모 해제"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(0, 0, 10, 10),
                          this, IDC_UNLOCK_BUTTON);
    m_unlockButton.SetFont(&m_buttonFont);
    return TRUE;
}

void CBreakOverlayWnd::Layout() {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(&wndTopMost, vx, vy, vw, vh, SWP_NOACTIVATE);

    // 주 모니터는 화면 좌표 (0,0) 에서 시작한다.
    m_primary.SetRect(-vx, -vy, -vx + GetSystemMetrics(SM_CXSCREEN), -vy + GetSystemMetrics(SM_CYSCREEN));

    const int bw = ScaleForDpi(88), bh = ScaleForDpi(30), margin = ScaleForDpi(28);
    m_unlockButton.MoveWindow(m_primary.right - bw - margin, m_primary.bottom - bh - margin, bw, bh);
}

void CBreakOverlayWnd::ShowBreak(int64_t remainingMs, int64_t totalMs) {
    const CString remaining = FormatDuration(remainingMs);
    double progress = totalMs > 0 ? static_cast<double>(remainingMs) / static_cast<double>(totalMs) : 0.0;
    progress = progress < 0 ? 0 : (progress > 1 ? 1 : progress);

    const bool changed = remaining != m_remaining || progress != m_progress;
    m_remaining = remaining;
    m_progress = progress;

    if (!IsWindowVisible()) {
        Layout();
        ShowWindow(SW_SHOW);
        SetForegroundWindow();
        MessageBeep(MB_ICONASTERISK);
    } else {
        // 다른 항상 위 창(작업 표시줄 등)이 덮지 못하도록 매초 다시 올린다.
        SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (changed) InvalidateRect(m_primary, FALSE);
}

void CBreakOverlayWnd::HideWnd() {
    if (IsWindowVisible()) ShowWindow(SW_HIDE);
    m_remaining.Empty();
}

BOOL CBreakOverlayWnd::OnEraseBkgnd(CDC*) { return TRUE; }

void CBreakOverlayWnd::OnClose() {
    // Alt+F4 등으로 닫지 못하게 무시
}

void CBreakOverlayWnd::OnSysCommand(UINT nID, LPARAM lParam) {
    const UINT cmd = nID & 0xFFF0;
    if (cmd == SC_CLOSE || cmd == SC_MINIMIZE || cmd == SC_MOVE || cmd == SC_SIZE) return;
    CWnd::OnSysCommand(nID, lParam);
}

void CBreakOverlayWnd::OnUnlockClicked() {
    if (m_pNotify) m_pNotify->PostMessage(WM_APP_UNLOCK_REQUEST);
}

// "부모 해제": 배경에 묻히는 얇은 테두리의 텍스트 버튼
void CBreakOverlayWnd::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis) {
    if (nIDCtl != IDC_UNLOCK_BUTTON) {
        CWnd::OnDrawItem(nIDCtl, dis);
        return;
    }
    CDC* dc = CDC::FromHandle(dis->hDC);
    CRect rc(dis->rcItem);
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    dc->FillSolidRect(rc, pressed ? kTrack : kBackground);

    CPen pen(PS_SOLID, 1, kTrack);
    CPen* oldPen = dc->SelectObject(&pen);
    CBrush* oldBrush = static_cast<CBrush*>(dc->SelectStockObject(NULL_BRUSH));
    const int radius = ScaleForDpi(8);
    dc->RoundRect(rc, CPoint(radius, radius));
    dc->SelectObject(oldBrush);
    dc->SelectObject(oldPen);

    CFont* oldFont = dc->SelectObject(&m_buttonFont);
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(kMuted);
    dc->DrawText(_T("부모 해제"), rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    dc->SelectObject(oldFont);
}

void CBreakOverlayWnd::OnPaint() {
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);

    CDC mem;
    mem.CreateCompatibleDC(&dc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* oldBmp = mem.SelectObject(&bmp);

    mem.FillSolidRect(rc, kBackground);
    mem.SetBkMode(TRANSPARENT);

    const CPoint c = m_primary.CenterPoint();
    const int halfW = m_primary.Width() / 2;

    // 남은 시간
    CFont* oldFont = mem.SelectObject(&m_timeFont);
    mem.SetTextColor(kText);
    CRect timeRect(c.x - halfW, c.y - ScaleForDpi(150), c.x + halfW, c.y + ScaleForDpi(10));
    mem.DrawText(m_remaining, timeRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // 진행 막대 (남은 만큼 채워짐)
    const int barW = ScaleForDpi(280), barH = ScaleForDpi(3);
    const CRect track(c.x - barW / 2, c.y + ScaleForDpi(28), c.x + barW / 2, c.y + ScaleForDpi(28) + barH);
    mem.FillSolidRect(track, kTrack);
    CRect fill = track;
    fill.right = fill.left + static_cast<int>(barW * m_progress + 0.5);
    if (fill.Width() > 0) mem.FillSolidRect(fill, kAccent);

    // 안내 문구
    mem.SelectObject(&m_labelFont);
    mem.SetTextColor(kMuted);
    CRect labelRect(c.x - halfW, track.bottom + ScaleForDpi(22), c.x + halfW, track.bottom + ScaleForDpi(56));
    mem.DrawText(_T("쉬는 시간이에요"), labelRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(oldFont);
    mem.SelectObject(oldBmp);
}
