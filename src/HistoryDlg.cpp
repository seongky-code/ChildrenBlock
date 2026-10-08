#include "pch.h"
#include "HistoryDlg.h"

#include "Common.h"

#include <algorithm>
#include <map>

namespace {

constexpr int kChartDays = 14;
constexpr int64_t kMinScaleMs = 60 * 60 * 1000;  // 기록이 적어도 1시간 눈금은 확보

const COLORREF kBar = RGB(166, 192, 240);
const COLORREF kBarToday = RGB(63, 111, 216);
const COLORREF kBarOver = RGB(240, 176, 72);
const COLORREF kAxis = RGB(214, 218, 224);
const COLORREF kLimit = RGB(150, 156, 167);
const COLORREF kMuted = RGB(120, 126, 136);
const COLORREF kText = RGB(40, 44, 52);

int ToDateInt(const CTime& t) { return t.GetYear() * 10000 + t.GetMonth() * 100 + t.GetDay(); }

CTime TodayNoon() {
    const CTime now = CTime::GetCurrentTime();
    return CTime(now.GetYear(), now.GetMonth(), now.GetDay(), 12, 0, 0);
}

CString FormatDate(int date) {
    static const LPCTSTR kWeekdays[] = {_T("일"), _T("월"), _T("화"), _T("수"), _T("목"), _T("금"), _T("토")};
    const int y = date / 10000, m = (date / 100) % 100, d = date % 100;
    CString text;
    if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1971 || y > 3000) {
        text.Format(_T("%d"), date);
        return text;
    }
    const CTime t(y, m, d, 12, 0, 0);
    text.Format(_T("%04d-%02d-%02d (%s)"), y, m, d, kWeekdays[t.GetDayOfWeek() - 1]);
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// CUsageChart

BEGIN_MESSAGE_MAP(CUsageChart, CStatic)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CUsageChart::SetData(const std::vector<Day>& days, int64_t limitMs) {
    m_days = days;
    m_limitMs = limitMs;
    if (!m_smallFont.GetSafeHandle()) {
        LOGFONT lf = {};
        lf.lfHeight = 80;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        _tcscpy_s(lf.lfFaceName, _T("맑은 고딕"));
        m_smallFont.CreatePointFontIndirect(&lf);
    }
    Invalidate();
}

BOOL CUsageChart::OnEraseBkgnd(CDC*) { return TRUE; }

void CUsageChart::OnPaint() {
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);

    CDC mem;
    mem.CreateCompatibleDC(&dc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* oldBmp = mem.SelectObject(&bmp);
    mem.FillSolidRect(rc, GetSysColor(COLOR_BTNFACE));
    mem.SetBkMode(TRANSPARENT);
    CFont* oldFont = mem.SelectObject(&m_smallFont);

    const int valueH = ScaleForDpi(16), labelH = ScaleForDpi(18);
    const CRect plot(rc.left, rc.top + valueH, rc.right, rc.bottom - labelH);
    const int n = static_cast<int>(m_days.size());

    int64_t scale = kMinScaleMs;
    for (const Day& d : m_days) scale = (std::max)(scale, d.usedMs);
    if (m_limitMs > 0) scale = (std::max)(scale, m_limitMs);
    scale = scale * 112 / 100;

    auto yFor = [&](int64_t ms) { return plot.bottom - static_cast<int>(plot.Height() * ms / scale); };

    // 하루 제한선
    if (m_limitMs > 0) {
        const int y = yFor(m_limitMs);
        CPen pen(PS_DOT, 1, kLimit);
        CPen* oldPen = mem.SelectObject(&pen);
        mem.MoveTo(plot.left, y);
        mem.LineTo(plot.right, y);
        mem.SelectObject(oldPen);
        mem.SetTextColor(kLimit);
        CRect r(plot.left, y - ScaleForDpi(15), plot.right, y - ScaleForDpi(1));
        mem.DrawText(_T("제한 ") + FormatHoursMinutesShort(m_limitMs), r, DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);
    }

    if (n > 0) {
        const int slot = plot.Width() / n;
        const int barW = (std::min)(slot * 56 / 100, ScaleForDpi(22));
        for (int i = 0; i < n; ++i) {
            const Day& d = m_days[i];
            const int cx = plot.left + slot * i + slot / 2;

            if (d.usedMs > 0) {
                int top = yFor(d.usedMs);
                if (plot.bottom - top < 2) top = plot.bottom - 2;
                const bool over = m_limitMs > 0 && d.usedMs > m_limitMs;
                mem.FillSolidRect(cx - barW / 2, top, barW, plot.bottom - top,
                                  over ? kBarOver : (d.today ? kBarToday : kBar));

                mem.SetTextColor(d.today ? kText : kMuted);
                CRect v(cx - slot / 2, top - valueH, cx + slot / 2, top - ScaleForDpi(2));
                mem.DrawText(FormatHoursMinutesShort(d.usedMs), v, DT_CENTER | DT_SINGLELINE | DT_BOTTOM);
            }

            CString label;
            if (d.today)
                label = _T("오늘");
            else
                label.Format(_T("%d"), d.dayOfMonth);
            mem.SetTextColor(d.today ? kText : kMuted);
            CRect l(cx - slot / 2, plot.bottom + ScaleForDpi(3), cx + slot / 2, rc.bottom);
            mem.DrawText(label, l, DT_CENTER | DT_SINGLELINE | DT_TOP);
        }
    }

    mem.FillSolidRect(plot.left, plot.bottom, plot.Width(), 1, kAxis);

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(oldFont);
    mem.SelectObject(oldBmp);
}

// ---------------------------------------------------------------------------
// CHistoryDlg

CHistoryDlg::CHistoryDlg(std::vector<UsageRecord> records, int64_t dailyLimitMs, CWnd* pParent)
    : CDialog(IDD_HISTORY, pParent), m_records(std::move(records)), m_limitMs(dailyLimitMs) {}

void CHistoryDlg::Show(const cb::ScheduleConfig& cfg, const UsageRecord* live, CWnd* pParent) {
    std::vector<UsageRecord> records = SettingsStore::LoadHistory();
    if (live) {
        auto it = std::find_if(records.begin(), records.end(),
                               [live](const UsageRecord& r) { return r.date == live->date; });
        if (it != records.end())
            *it = *live;
        else
            records.push_back(*live);
    }
    const int64_t limit = cfg.dailyLimitEnabled ? static_cast<int64_t>(cfg.dailyLimitMinutes) * 60 * 1000 : -1;
    CHistoryDlg dlg(std::move(records), limit, pParent);
    dlg.DoModal();
}

void CHistoryDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_HISTORY_LIST, m_list);
}

BOOL CHistoryDlg::OnInitDialog() {
    CDialog::OnInitDialog();
    ApplyAppIcon(this);

    std::map<int, UsageRecord> byDate;
    for (const UsageRecord& r : m_records) byDate[r.date] = r;
    auto usedOn = [&byDate](int date) {
        auto it = byDate.find(date);
        return it == byDate.end() ? 0 : it->second.usedMs;
    };

    // 그래프: 최근 14일 (기록 없는 날은 0)
    const CTime today = TodayNoon();
    std::vector<CUsageChart::Day> days;
    for (int i = kChartDays - 1; i >= 0; --i) {
        const CTime t = today - CTimeSpan(i, 0, 0, 0);
        CUsageChart::Day d;
        d.dayOfMonth = t.GetDay();
        d.usedMs = usedOn(ToDateInt(t));
        d.today = i == 0;
        days.push_back(d);
    }
    m_chart.SubclassDlgItem(IDC_HISTORY_CHART, this);
    m_chart.SetData(days, m_limitMs);

    // 요약
    int64_t sum7 = 0, sum30 = 0;
    for (int i = 0; i < 30; ++i) {
        const int64_t used = usedOn(ToDateInt(today - CTimeSpan(i, 0, 0, 0)));
        sum30 += used;
        if (i < 7) sum7 += used;
    }
    CString summary;
    summary.Format(_T("최근 7일 평균 %s   ·   최근 30일 합계 %s"), FormatHoursMinutesLong(sum7 / 7).GetString(),
                   FormatHoursMinutesLong(sum30).GetString());
    SetDlgItemText(IDC_HISTORY_SUMMARY, summary);

    // 목록: 최근 날짜부터
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    m_list.InsertColumn(0, _T("날짜"), LVCFMT_LEFT, ScaleForDpi(170));
    m_list.InsertColumn(1, _T("사용 시간"), LVCFMT_RIGHT, ScaleForDpi(120));
    m_list.InsertColumn(2, _T("연장"), LVCFMT_RIGHT, ScaleForDpi(90));

    int row = 0;
    for (auto it = byDate.rbegin(); it != byDate.rend(); ++it, ++row) {
        const UsageRecord& r = it->second;
        m_list.InsertItem(row, FormatDate(r.date));
        m_list.SetItemText(row, 1, FormatHoursMinutesLong(r.usedMs));
        m_list.SetItemText(row, 2, r.bonusMs > 0 ? FormatHoursMinutesLong(r.bonusMs) : CString(_T("-")));
    }
    m_list.SetColumnWidth(2, LVSCW_AUTOSIZE_USEHEADER);  // 마지막 열을 남은 폭까지 늘림

    SetForegroundWindow();
    return TRUE;
}
