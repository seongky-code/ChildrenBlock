#include "pch.h"
#include "Protocol.h"

namespace proto {

namespace {

std::string ToUtf8(const CString& s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), &out[0], n, nullptr, nullptr);
    return out;
}

CString FromUtf8(const char* s, int len) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    CString out;
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s, len, out.GetBuffer(n), n);
        out.ReleaseBuffer(n);
    }
    return out;
}

CString StripNewlines(CString s) {
    s.Remove(_T('\r'));
    s.Remove(_T('\n'));
    return s;
}

}  // namespace

CString PipeName() {
#ifdef _DEBUG
    TCHAR name[256] = {};
    if (GetEnvironmentVariable(_T("CHILDRENBLOCK_PIPE"), name, _countof(name)) > 0)
        return CString(_T("\\\\.\\pipe\\")) + name;
#endif
    return _T("\\\\.\\pipe\\ChildrenBlock");
}

void Message::Set(LPCTSTR key, const CString& value) {
    for (auto& f : m_fields) {
        if (f.first == key) {
            f.second = StripNewlines(value);
            return;
        }
    }
    Add(key, value);
}

void Message::Set(LPCTSTR key, int64_t value) {
    CString text;
    text.Format(_T("%lld"), value);
    Set(key, text);
}

void Message::Add(LPCTSTR key, const CString& value) { m_fields.emplace_back(key, StripNewlines(value)); }

bool Message::Has(LPCTSTR key) const {
    for (const auto& f : m_fields)
        if (f.first == key) return true;
    return false;
}

CString Message::Get(LPCTSTR key, LPCTSTR def) const {
    for (const auto& f : m_fields)
        if (f.first == key) return f.second;
    return def;
}

int64_t Message::GetInt(LPCTSTR key, int64_t def) const {
    for (const auto& f : m_fields)
        if (f.first == key) return _tcstoi64(f.second, nullptr, 10);
    return def;
}

std::vector<CString> Message::GetAll(LPCTSTR key) const {
    std::vector<CString> values;
    for (const auto& f : m_fields)
        if (f.first == key) values.push_back(f.second);
    return values;
}

std::string Message::Encode() const {
    CString text = StripNewlines(m_head) + _T("\n");
    for (const auto& f : m_fields) text += f.first + _T("=") + f.second + _T("\n");
    return ToUtf8(text);
}

bool Message::Decode(const char* data, size_t size, Message& out) {
    out = Message();
    const CString text = FromUtf8(data, static_cast<int>(size));
    int pos = 0;
    bool first = true;
    while (pos <= text.GetLength()) {
        int end = text.Find(_T('\n'), pos);
        if (end < 0) end = text.GetLength();
        CString line = text.Mid(pos, end - pos);
        line.TrimRight(_T('\r'));
        pos = end + 1;
        if (first) {
            out.m_head = line;
            first = false;
        } else if (!line.IsEmpty()) {
            const int eq = line.Find(_T('='));
            if (eq <= 0) continue;
            out.m_fields.emplace_back(line.Left(eq), line.Mid(eq + 1));
        }
    }
    return !out.m_head.IsEmpty();
}

Message Message::Error(LPCTSTR code, LPCTSTR text) {
    Message m(_T("ERR"));
    m.Set(_T("code"), code);
    if (text && *text) m.Set(_T("message"), text);
    return m;
}

void ConfigToMessage(const cb::ScheduleConfig& c, Message& m) {
    m.Set(_T("breaksEnabled"), c.breaksEnabled ? 1 : 0);
    m.Set(_T("useMinutes"), c.useMinutes);
    m.Set(_T("breakMinutes"), c.breakMinutes);
    m.Set(_T("breakWarnSeconds"), c.breakWarnSeconds);
    m.Set(_T("dailyLimitEnabled"), c.dailyLimitEnabled ? 1 : 0);
    m.Set(_T("dailyLimitMinutes"), c.dailyLimitMinutes);
    m.Set(_T("shutdownAtEnabled"), c.shutdownAtEnabled ? 1 : 0);
    m.Set(_T("shutdownAtMinute"), c.shutdownAtMinuteOfDay);
    m.Set(_T("shutdownWarnSeconds"), c.shutdownWarnSeconds);
    m.Set(_T("countBreakAsUsage"), c.countBreakAsUsage ? 1 : 0);
}

cb::ScheduleConfig ConfigFromMessage(const Message& m) {
    const cb::ScheduleConfig d;
    cb::ScheduleConfig c;
    c.breaksEnabled = m.GetInt(_T("breaksEnabled"), d.breaksEnabled) != 0;
    c.useMinutes = static_cast<int>(m.GetInt(_T("useMinutes"), d.useMinutes));
    c.breakMinutes = static_cast<int>(m.GetInt(_T("breakMinutes"), d.breakMinutes));
    c.breakWarnSeconds = static_cast<int>(m.GetInt(_T("breakWarnSeconds"), d.breakWarnSeconds));
    c.dailyLimitEnabled = m.GetInt(_T("dailyLimitEnabled"), d.dailyLimitEnabled) != 0;
    c.dailyLimitMinutes = static_cast<int>(m.GetInt(_T("dailyLimitMinutes"), d.dailyLimitMinutes));
    c.shutdownAtEnabled = m.GetInt(_T("shutdownAtEnabled"), d.shutdownAtEnabled) != 0;
    c.shutdownAtMinuteOfDay = static_cast<int>(m.GetInt(_T("shutdownAtMinute"), d.shutdownAtMinuteOfDay));
    c.shutdownWarnSeconds = static_cast<int>(m.GetInt(_T("shutdownWarnSeconds"), d.shutdownWarnSeconds));
    c.countBreakAsUsage = m.GetInt(_T("countBreakAsUsage"), d.countBreakAsUsage) != 0;
    return c;
}

Status ParseStatus(const Message& m) {
    Status s;
    s.connected = true;
    s.tracked = m.GetInt(_T("tracked")) != 0;
    s.hasPassword = m.GetInt(_T("hasPassword")) != 0;
    s.paused = m.GetInt(_T("paused")) != 0;
    s.dryRun = m.GetInt(_T("dryRun")) != 0;
    s.user = m.Get(_T("user"));
    s.engine.phase = static_cast<cb::Phase>(m.GetInt(_T("phase")));
    s.engine.msToBreak = m.GetInt(_T("toBreak"), -1);
    s.engine.msBreakRemaining = m.GetInt(_T("breakRemaining"));
    s.engine.msToShutdown = m.GetInt(_T("toShutdown"), -1);
    s.engine.usedTodayMs = m.GetInt(_T("usedToday"));
    s.bonusMs = m.GetInt(_T("bonus"));
    s.breakTotalMs = m.GetInt(_T("breakTotal"));
    s.breakWarnTotalMs = m.GetInt(_T("breakWarnTotal"));
    s.shutdownWarnTotalMs = m.GetInt(_T("shutdownWarnTotal"));
    return s;
}

bool Transact(const Message& request, Message& response, DWORD timeoutMs) {
    const CString name = PipeName();
    // 서비스는 IU(대화형 사용자)에게 읽기·쓰기·특성 쓰기만 허용한다 (파이프 인스턴스 생성 권한 없음).
    const DWORD access = GENERIC_READ | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 2 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        pipe = CreateFile(name, access, 0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipe(name, timeoutMs)) return false;
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) return false;

#ifndef _DEBUG
    // 서비스(세션 0)가 아닌 프로세스가 같은 이름의 파이프를 먼저 만들었을 수 있다.
    ULONG serverSession = 0;
    if (!GetNamedPipeServerSessionId(pipe, &serverSession) || serverSession != 0) {
        CloseHandle(pipe);
        return false;
    }
#endif

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        CloseHandle(pipe);
        return false;
    }

    const std::string out = request.Encode();
    std::vector<char> buf(64 * 1024);
    std::string in;

    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    DWORD read = 0;
    BOOL ok = TransactNamedPipe(pipe, const_cast<char*>(out.data()), static_cast<DWORD>(out.size()), buf.data(),
                                static_cast<DWORD>(buf.size()), &read, &ov);
    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && err == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, timeoutMs) != WAIT_OBJECT_0) {
            CancelIo(pipe);
            WaitForSingleObject(ov.hEvent, 1000);
            CloseHandle(ov.hEvent);
            CloseHandle(pipe);
            return false;
        }
        ok = GetOverlappedResult(pipe, &ov, &read, FALSE);
        err = ok ? ERROR_SUCCESS : GetLastError();
    }
    in.append(buf.data(), read);

    // 응답이 버퍼보다 크면 나머지를 이어서 읽는다.
    while (!ok && err == ERROR_MORE_DATA) {
        ResetEvent(ov.hEvent);
        read = 0;
        ok = ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), &read, &ov);
        err = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && err == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, timeoutMs) != WAIT_OBJECT_0) {
                CancelIo(pipe);
                break;
            }
            ok = GetOverlappedResult(pipe, &ov, &read, FALSE);
            err = ok ? ERROR_SUCCESS : GetLastError();
        }
        in.append(buf.data(), read);
    }

    CloseHandle(ov.hEvent);
    CloseHandle(pipe);
    return ok && Message::Decode(in.data(), in.size(), response);
}

}  // namespace proto
