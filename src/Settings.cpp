#include "pch.h"
#include "Settings.h"

#include "Common.h"

#include <algorithm>

namespace {

CString SettingsPath() { return GetDataDir() + _T("\\settings.ini"); }
CString StatePath() { return GetDataDir() + _T("\\state.ini"); }

int ReadInt(LPCTSTR section, LPCTSTR key, int def, const CString& path) {
    return static_cast<int>(GetPrivateProfileInt(section, key, def, path));
}

int64_t ReadInt64(LPCTSTR section, LPCTSTR key, const CString& path) {
    TCHAR buf[32] = {};
    GetPrivateProfileString(section, key, _T("0"), buf, _countof(buf), path);
    return _tcstoi64(buf, nullptr, 10);
}

CString ReadString(LPCTSTR section, LPCTSTR key, const CString& path) {
    TCHAR buf[256] = {};
    GetPrivateProfileString(section, key, _T(""), buf, _countof(buf), path);
    return buf;
}

bool WriteInt64(LPCTSTR section, LPCTSTR key, int64_t value, const CString& path) {
    CString text;
    text.Format(_T("%lld"), value);
    return WritePrivateProfileString(section, key, text, path) != FALSE;
}

int Clamp(int v, int lo, int hi) { return (std::min)((std::max)(v, lo), hi); }

void Sanitize(cb::ScheduleConfig& c) {
    using namespace Limits;
    c.useMinutes = Clamp(c.useMinutes, kUseMinMin, kUseMinMax);
    c.breakMinutes = Clamp(c.breakMinutes, kBreakMinMin, kBreakMinMax);
    c.breakWarnSeconds = Clamp(c.breakWarnSeconds, kBreakWarnSecMin, kBreakWarnSecMax);
    c.dailyLimitMinutes = Clamp(c.dailyLimitMinutes, kDailyLimitMinMin, kDailyLimitMinMax);
    c.shutdownAtMinuteOfDay = Clamp(c.shutdownAtMinuteOfDay, 0, 24 * 60 - 1);
    c.shutdownWarnSeconds = Clamp(c.shutdownWarnSeconds, kShutWarnMinMin * 60, kShutWarnMinMax * 60);
}

}  // namespace

namespace SettingsStore {

void Load(AppSettings& s) {
    const CString path = SettingsPath();
    cb::ScheduleConfig& c = s.schedule;
    const cb::ScheduleConfig def;

    c.breaksEnabled = ReadInt(_T("Schedule"), _T("BreaksEnabled"), def.breaksEnabled, path) != 0;
    c.useMinutes = ReadInt(_T("Schedule"), _T("UseMinutes"), def.useMinutes, path);
    c.breakMinutes = ReadInt(_T("Schedule"), _T("BreakMinutes"), def.breakMinutes, path);
    c.breakWarnSeconds = ReadInt(_T("Schedule"), _T("BreakWarnSeconds"), def.breakWarnSeconds, path);
    c.dailyLimitEnabled = ReadInt(_T("Schedule"), _T("DailyLimitEnabled"), def.dailyLimitEnabled, path) != 0;
    c.dailyLimitMinutes = ReadInt(_T("Schedule"), _T("DailyLimitMinutes"), def.dailyLimitMinutes, path);
    c.shutdownAtEnabled = ReadInt(_T("Schedule"), _T("ShutdownAtEnabled"), def.shutdownAtEnabled, path) != 0;
    c.shutdownAtMinuteOfDay = ReadInt(_T("Schedule"), _T("ShutdownAtMinute"), def.shutdownAtMinuteOfDay, path);
    c.shutdownWarnSeconds = ReadInt(_T("Schedule"), _T("ShutdownWarnSeconds"), def.shutdownWarnSeconds, path);
    c.countBreakAsUsage = ReadInt(_T("Schedule"), _T("CountBreakAsUsage"), def.countBreakAsUsage, path) != 0;
    Sanitize(c);

    s.passwordSalt = ReadString(_T("Security"), _T("Salt"), path);
    s.passwordHash = ReadString(_T("Security"), _T("Hash"), path);
    s.passwordIterations = ReadInt(_T("Security"), _T("Iterations"), 0, path);
}

bool Save(const AppSettings& s) {
    const CString path = SettingsPath();
    const cb::ScheduleConfig& c = s.schedule;
    bool ok = true;
    ok &= WriteInt64(_T("Schedule"), _T("BreaksEnabled"), c.breaksEnabled ? 1 : 0, path);
    ok &= WriteInt64(_T("Schedule"), _T("UseMinutes"), c.useMinutes, path);
    ok &= WriteInt64(_T("Schedule"), _T("BreakMinutes"), c.breakMinutes, path);
    ok &= WriteInt64(_T("Schedule"), _T("BreakWarnSeconds"), c.breakWarnSeconds, path);
    ok &= WriteInt64(_T("Schedule"), _T("DailyLimitEnabled"), c.dailyLimitEnabled ? 1 : 0, path);
    ok &= WriteInt64(_T("Schedule"), _T("DailyLimitMinutes"), c.dailyLimitMinutes, path);
    ok &= WriteInt64(_T("Schedule"), _T("ShutdownAtEnabled"), c.shutdownAtEnabled ? 1 : 0, path);
    ok &= WriteInt64(_T("Schedule"), _T("ShutdownAtMinute"), c.shutdownAtMinuteOfDay, path);
    ok &= WriteInt64(_T("Schedule"), _T("ShutdownWarnSeconds"), c.shutdownWarnSeconds, path);
    ok &= WriteInt64(_T("Schedule"), _T("CountBreakAsUsage"), c.countBreakAsUsage ? 1 : 0, path);

    ok &= WritePrivateProfileString(_T("Security"), _T("Salt"), s.passwordSalt, path) != FALSE;
    ok &= WritePrivateProfileString(_T("Security"), _T("Hash"), s.passwordHash, path) != FALSE;
    ok &= WriteInt64(_T("Security"), _T("Iterations"), s.passwordIterations, path);
    return ok;
}

ULONGLONG SettingsFileStamp() {
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesEx(SettingsPath(), GetFileExInfoStandard, &info)) return 0;
    return (static_cast<ULONGLONG>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
}

bool LoadState(SavedState& st) {
    const CString path = StatePath();
    if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) return false;
    st.date = ReadInt(_T("State"), _T("Date"), 0, path);
    st.engine.usedTodayMs = ReadInt64(_T("State"), _T("UsedTodayMs"), path);
    st.engine.cycleUsedMs = ReadInt64(_T("State"), _T("CycleUsedMs"), path);
    st.engine.breakRemainingMs = ReadInt64(_T("State"), _T("BreakRemainingMs"), path);
    st.engine.bonusMs = ReadInt64(_T("State"), _T("BonusMs"), path);
    return true;
}

bool SaveState(const SavedState& st) {
    const CString path = StatePath();
    bool ok = true;
    ok &= WriteInt64(_T("State"), _T("Date"), st.date, path);
    ok &= WriteInt64(_T("State"), _T("UsedTodayMs"), st.engine.usedTodayMs, path);
    ok &= WriteInt64(_T("State"), _T("CycleUsedMs"), st.engine.cycleUsedMs, path);
    ok &= WriteInt64(_T("State"), _T("BreakRemainingMs"), st.engine.breakRemainingMs, path);
    ok &= WriteInt64(_T("State"), _T("BonusMs"), st.engine.bonusMs, path);

    if (st.date > 0) {
        CString key, value;
        key.Format(_T("%d"), st.date);
        value.Format(_T("%lld,%lld"), st.engine.usedTodayMs, st.engine.bonusMs);
        ok &= WritePrivateProfileString(_T("History"), key, value, path) != FALSE;
    }
    return ok;
}

std::vector<UsageRecord> LoadHistory() {
    const CString path = StatePath();
    std::vector<TCHAR> buf(16 * 1024);
    for (;;) {
        const DWORD n = GetPrivateProfileSection(_T("History"), buf.data(), static_cast<DWORD>(buf.size()), path);
        if (n < buf.size() - 2) break;
        buf.resize(buf.size() * 2);  // 버퍼가 모자라면 n == size-2
    }

    std::vector<UsageRecord> records;
    for (const TCHAR* p = buf.data(); *p; p += _tcslen(p) + 1) {
        const CString line(p);
        const int eq = line.Find(_T('='));
        if (eq <= 0) continue;
        UsageRecord r;
        r.date = _ttoi(line.Left(eq));
        if (r.date < 19000101 || r.date > 99991231) continue;
        const CString value = line.Mid(eq + 1);
        const int comma = value.Find(_T(','));
        r.usedMs = _tcstoi64(comma >= 0 ? value.Left(comma) : value, nullptr, 10);
        r.bonusMs = comma >= 0 ? _tcstoi64(value.Mid(comma + 1), nullptr, 10) : 0;
        records.push_back(r);
    }
    std::sort(records.begin(), records.end(),
              [](const UsageRecord& a, const UsageRecord& b) { return a.date < b.date; });
    return records;
}

void PruneHistory(size_t keepDays) {
    const std::vector<UsageRecord> records = LoadHistory();
    if (records.size() <= keepDays) return;
    const CString path = StatePath();
    for (size_t i = 0; i < records.size() - keepDays; ++i) {
        CString key;
        key.Format(_T("%d"), records[i].date);
        WritePrivateProfileString(_T("History"), key, nullptr, path);
    }
}

}  // namespace SettingsStore
