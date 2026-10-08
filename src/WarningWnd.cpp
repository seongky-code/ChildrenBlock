#include "pch.h"
#include "WarningWnd.h"

#include "Common.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace {

// 휴식 화면과 같은 색 계열
const COLORREF kCard = RGB(30, 33, 38);
const COLORREF kBorder = RGB(54, 59, 67);
const COLORREF kText = RGB(236, 238, 241);
const COLORREF kMuted = RGB(140, 146, 157);
const COLORREF kTrack = RGB(54, 59, 67);

// Windows 11 DWM 속성 (구버전 SDK 대비 숫자로 정의, Windows 10 에서는 무시됨)
constexpr DWORD kDwmCornerPreference = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kDwmBorderColor = 34;       // DWMWA_BORDER_COLOR
constexpr int kDwmCornerRound = 2;          // DWMWCP_ROUND

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

BEGIN_MESSAGE_MAP(CWarningWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CLOSE()
    ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

BOOL CWarningWnd::CreateWnd() {
    const CString cls =
        AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW, ::LoadCursor(nullptr, IDC_ARROW));
    if (!CreateEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, _T("ChildrenBlock 알림"), WS_POPUP, 0, 0,
                  10, 10, nullptr, 0)) {
        return FALSE;
    }

    const int corner = kDwmCornerRound;
    DwmSetWindowAttribute(m_hWnd, kDwmCornerPreference, &corner, sizeof(corner));
    const COLORREF border = kBorder;
    DwmSetWindowAttribute(m_hWnd, kDwmBorderColor, &border, sizeof(border));

    CreateUiFont(m_titleFont, _T("맑은 고딕"), 100, FW_NORMAL);
    CreateUiFont(m_timeFont, _T("맑은 고딕 Semilight"), 300, FW_LIGHT);
    CreateUiFont(m_hintFont, _T("맑은 고딕"), 95, FW_NORMAL);
    return TRUE;
}

void CWarningWnd::ShowCountdown(const CString& title, int64_t remainingMs, int64_t totalMs, const CString& hint,
                                COLORREF accent) {
    double progress = totalMs > 0 ? static_cast<double>(remainingMs) / static_cast<double>(totalMs) : 0.0;
    progress = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
    const CString time = FormatDuration(remainingMs);

    const bool titleChanged = title != m_title;
    const bool changed =
        titleChanged || time != m_time || hint != m_hint || accent != m_accent || progress != m_progress;
    m_title = title;
    m_time = time;
    m_hint = hint;
    m_accent = accent;
    m_progress = progress;

    if (!IsWindowVisible()) {
        const int w = ScaleForDpi(300), h = ScaleForDpi(142), margin = ScaleForDpi(16);
        CRect work;
        SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(&wndTopMost, work.right - w - margin, work.bottom - h - margin, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if (titleChanged) MessageBeep(MB_ICONEXCLAMATION);
    if (changed) Invalidate(FALSE);
}

void CWarningWnd::HideWnd() {
    if (IsWindowVisible()) ShowWindow(SW_HIDE);
    m_title.Empty();
}

int CWarningWnd::OnMouseActivate(CWnd*, UINT, UINT) { return MA_NOACTIVATE; }

void CWarningWnd::OnClose() {
    // taskkill 등이 보내는 WM_CLOSE 무시 (창은 CTrayWnd 가 관리)
}

BOOL CWarningWnd::OnEraseBkgnd(CDC*) { return TRUE; }

//  ● 곧 쉬는 시간이에요
//  0:43
//  ━━━━━━━━━━━━━━━━──────────
//  하던 일을 정리해 주세요
void CWarningWnd::OnPaint() {
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);

    CDC mem;
    mem.CreateCompatibleDC(&dc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* oldBmp = mem.SelectObject(&bmp);

    mem.FillSolidRect(rc, kCard);
    {  // Windows 10 처럼 DWM 테두리가 없을 때를 위한 1px 테두리
        CPen pen(PS_SOLID, 1, kBorder);
        CPen* oldPen = mem.SelectObject(&pen);
        CBrush* oldBrush = static_cast<CBrush*>(mem.SelectStockObject(NULL_BRUSH));
        mem.Rectangle(rc);
        mem.SelectObject(oldBrush);
        mem.SelectObject(oldPen);
    }
    mem.SetBkMode(TRANSPARENT);

    const int pad = ScaleForDpi(20);
    const int left = pad, right = rc.right - pad;

    // 강조색 점 + 제목
    const int dot = ScaleForDpi(8);
    const int titleTop = ScaleForDpi(16), titleH = ScaleForDpi(20);
    {
        CBrush brush(m_accent);
        CPen pen(PS_SOLID, 1, m_accent);
        CBrush* oldBrush = mem.SelectObject(&brush);
        CPen* oldPen = mem.SelectObject(&pen);
        const int dotTop = titleTop + (titleH - dot) / 2;
        mem.Ellipse(left, dotTop, left + dot, dotTop + dot);
        mem.SelectObject(oldPen);
        mem.SelectObject(oldBrush);
    }
    CFont* oldFont = mem.SelectObject(&m_titleFont);
    mem.SetTextColor(kMuted);
    CRect titleRect(left + dot + ScaleForDpi(8), titleTop, right, titleTop + titleH);
    mem.DrawText(m_title, titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    // 남은 시간
    mem.SelectObject(&m_timeFont);
    mem.SetTextColor(kText);
    CRect timeRect(left - ScaleForDpi(2), titleRect.bottom + ScaleForDpi(2), right, titleRect.bottom + ScaleForDpi(50));
    mem.DrawText(m_time, timeRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // 진행 막대 (남은 만큼 채워짐)
    const int barTop = timeRect.bottom + ScaleForDpi(6), barH = ScaleForDpi(3);
    const CRect track(left, barTop, right, barTop + barH);
    mem.FillSolidRect(track, kTrack);
    CRect fill = track;
    fill.right = fill.left + static_cast<int>(track.Width() * m_progress + 0.5);
    if (fill.Width() > 0) mem.FillSolidRect(fill, m_accent);

    // 안내 문구
    mem.SelectObject(&m_hintFont);
    mem.SetTextColor(kMuted);
    CRect hintRect(left, track.bottom + ScaleForDpi(10), right, track.bottom + ScaleForDpi(30));
    mem.DrawText(m_hint, hintRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(oldFont);
    mem.SelectObject(oldBmp);
}
