#include "pch.h"
#include "Controller.h"

#include "Common.h"
#include "PasswordHasher.h"

namespace {

constexpr int kSaveEveryTicks = 30;
constexpr int kReloadCheckTicks = 5;
constexpr int64_t kMaxTickDeltaMs = 5000;  // 이보다 길면 절전/정지로 보고 1초만 계산
constexpr int kMaxFailedAttempts = 5;
constexpr ULONGLONG kLockoutMs = 5 * 60 * 1000;
constexpr ULONGLONG kForceShutdownAfterMs = 60 * 1000;
constexpr ULONGLONG kRelaunchDelayMs = 1000;
constexpr ULONGLONG kAlreadyRunningRetryMs = 10 * 1000;
constexpr ULONGLONG kLaunchRetryMs = 3000;

LPCTSTR PhaseName(cb::Phase p) {
    switch (p) {
    case cb::Phase::Using: return _T("사용");
    case cb::Phase::BreakWarning: return _T("휴식 임박");
    case cb::Phase::Break: return _T("휴식");
    case cb::Phase::ShutdownWarning: return _T("종료 임박");
    case cb::Phase::Shutdown: return _T("종료");
    }
    return _T("?");
}

void Now(int& today, int64_t& secondsOfDay) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    today = TodayAsInt(st);
    secondsOfDay = SecondsOfDay(st);
}

}  // namespace

Controller::Controller(const ControllerOptions& options) : m_opt(options) {}

bool Controller::Start() {
    SettingsStore::Load(m_settings);
    m_settingsStamp = SettingsStore::SettingsFileStamp();
    m_engine.SetConfig(m_settings.schedule);
    m_lastTick = GetTickCount64();

    if (!m_server.Start(proto::PipeName(), [this](const proto::Message& req, const PipeClientInfo& client) {
            return Handle(req, client);
        })) {
        return false;
    }

    m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_thread = std::thread(&Controller::Run, this);

    CString users;
    for (const CString& u : m_settings.childUsers) users += (users.IsEmpty() ? _T("") : _T(", ")) + u;
    LogEvent(_T("서비스 시작 (자녀 계정: %s)%s%s"), m_opt.everyoneIsChild ? _T("모든 계정") : users.GetString(),
             m_opt.asService ? _T("") : _T(" [콘솔]"), m_opt.dryRun ? _T(" [테스트 모드]") : _T(""));
    return true;
}

void Controller::Stop() {
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
        if (m_thread.joinable()) m_thread.join();
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    m_server.Stop();

    std::lock_guard<std::mutex> lock(m_mutex);
    SaveState();
    TerminateTrays();
    LogEvent(_T("서비스 중지"));
}

void Controller::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    SaveState();
    LogEvent(_T("Windows 종료 - 상태 저장"));
}

void Controller::Run() {
    while (WaitForSingleObject(m_stopEvent, 1000) == WAIT_TIMEOUT) Tick();
}

// ---------------------------------------------------------------------------
// 1초 주기 처리

bool Controller::IsChild(const CString& user) const {
    return !user.IsEmpty() && (m_opt.everyoneIsChild || m_settings.IsChild(user));
}

void Controller::Tick() {
    const ULONGLONG now = GetTickCount64();
    int64_t delta = static_cast<int64_t>(now - m_lastTick);
    m_lastTick = now;
    if (delta > kMaxTickDeltaMs) delta = 1000;
    delta *= m_opt.speed;

    int today = 0;
    int64_t sod = 0;
    Now(today, sod);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (++m_ticksSinceReload >= kReloadCheckTicks) ReloadSettingsIfChanged();

    const SessionInfo session = QueryActiveConsoleSession();
    const bool isChild = session.active && IsChild(session.user);
    if (!isChild) {
        if (!m_user.IsEmpty()) EndTracking();
    } else if (m_user.CompareNoCase(session.user) != 0 || m_sessionId != session.id) {
        if (!m_user.IsEmpty()) EndTracking();
        BeginTracking(session, today, sod);
    }
    if (m_user.IsEmpty()) {
        m_shutdownIssuedAt = 0;
        m_shutdownForced = false;
        return;
    }

    if (today != m_today) {
        SaveState();  // 어제 기록을 마지막 값으로 확정
        m_engine.ResetDay();
        m_paused = false;
        m_engine.SetSuspended(false);
        m_today = today;
        LogEvent(_T("[%s] 날짜 변경 - 오늘 사용 시간 초기화"), m_user.GetString());
    }

    // 잠금 화면에서는 사용 시간을 세지 않는다. 단, 휴식은 계속 진행된다.
    const cb::Phase before = m_engine.GetPhase();
    const bool counting = !session.locked || before == cb::Phase::Break;
    m_engine.Advance(counting ? delta : 0, sod);
    const cb::Status status = m_engine.GetStatus();

    if (status.phase != before) {
        LogEvent(_T("[%s] 상태 변경: %s → %s (오늘 사용 %s)"), m_user.GetString(), PhaseName(before),
                 PhaseName(status.phase), FormatDuration(status.usedTodayMs).GetString());
        SaveState();
    } else if (++m_ticksSinceSave >= kSaveEveryTicks) {
        SaveState();
    }

    WatchTray(session);

    if (status.phase == cb::Phase::Shutdown) {
        DoShutdown();
    } else {
        m_shutdownIssuedAt = 0;
        m_shutdownForced = false;
    }
}

void Controller::BeginTracking(const SessionInfo& session, int today, int64_t secondsOfDay) {
    m_user = session.user;
    m_sessionId = session.id;
    m_today = today;

    SavedState saved;
    cb::PersistentState restore;
    m_paused = false;
    if (SettingsStore::LoadState(m_user, saved) && saved.date == today) {
        restore = saved.engine;
        m_paused = saved.pausedDate == today;
    }
    m_engine.SetSuspended(false);
    m_engine.SetConfig(m_settings.schedule);
    m_engine.Restore(restore, secondsOfDay);
    m_engine.SetSuspended(m_paused);
    SettingsStore::PruneHistory(m_user, kHistoryKeepDays);

    // 이전 로그온에서 남은 (이미 끝난) 트레이 핸들은 조용히 정리
    auto it = m_trays.find(session.id);
    if (it != m_trays.end() && it->second.process &&
        WaitForSingleObject(it->second.process, 0) != WAIT_TIMEOUT) {
        CloseHandle(it->second.process);
        it->second.process = nullptr;
    }

    LogEvent(_T("[%s] 자녀 세션 시작 (세션 %lu, 오늘 사용 %s)%s"), m_user.GetString(), session.id,
             FormatDuration(restore.usedTodayMs).GetString(), m_paused ? _T(" [오늘 제한 해제]") : _T(""));
    SaveState();
}

void Controller::EndTracking() {
    SaveState();
    LogEvent(_T("[%s] 자녀 세션 종료 또는 전환"), m_user.GetString());
    m_user.Empty();
    m_sessionId = static_cast<DWORD>(-1);
}

void Controller::SaveState() {
    if (m_user.IsEmpty()) return;
    SavedState st;
    st.date = m_today;
    st.pausedDate = m_paused ? m_today : 0;
    st.engine = m_engine.Save();
    SettingsStore::SaveState(m_user, st);
    m_ticksSinceSave = 0;
}

void Controller::Refresh() {
    int today = 0;
    int64_t sod = 0;
    Now(today, sod);
    m_engine.Advance(0, sod);
}

void Controller::ReloadSettingsIfChanged() {
    m_ticksSinceReload = 0;
    const ULONGLONG stamp = SettingsStore::SettingsFileStamp();
    if (stamp == 0 || stamp == m_settingsStamp) return;
    m_settingsStamp = stamp;
    SettingsStore::Load(m_settings);
    m_engine.SetConfig(m_settings.schedule);
    LogEvent(_T("설정 파일 변경 감지 - 다시 읽음"));
}

// ---------------------------------------------------------------------------
// 트레이 앱 실행·감시

void Controller::WatchTray(const SessionInfo& session) {
    const ULONGLONG now = GetTickCount64();
    Tray& tray = m_trays[session.id];

    if (tray.process) {
        if (WaitForSingleObject(tray.process, 0) == WAIT_TIMEOUT) return;  // 실행 중

        DWORD code = 0;
        GetExitCodeProcess(tray.process, &code);
        CloseHandle(tray.process);
        tray.process = nullptr;

        if (code == static_cast<DWORD>(kExitAlreadyRunning)) {
            tray.nextLaunch = now + kAlreadyRunningRetryMs;  // 직접 실행한 트레이가 이미 있음
        } else {
            LogEvent(_T("[%s] 트레이 앱 종료 감지 (코드 %lu) - 다시 실행"), m_user.GetString(), code);
            tray.nextLaunch = now + kRelaunchDelayMs;
            if (m_engine.GetPhase() == cb::Phase::Break) {
                LogEvent(_T("[%s] 휴식 중 트레이 종료 → 세션 잠금%s"), m_user.GetString(),
                         m_opt.dryRun ? _T(" (테스트 모드: 생략)") : _T(""));
                if (!m_opt.dryRun) WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, session.id, FALSE);
            }
        }
    }

    if (now < tray.nextLaunch) return;
    tray.nextLaunch = now + kLaunchRetryMs;
    HANDLE process = nullptr;
    if (LaunchTray(session.id, m_opt.asService, m_opt.protectTray, process)) tray.process = process;
}

void Controller::TerminateTrays() {
    for (auto& entry : m_trays) {
        if (entry.second.process) {
            TerminateProcess(entry.second.process, 0);
            CloseHandle(entry.second.process);
            entry.second.process = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// PC 종료

void Controller::DoShutdown() {
    const ULONGLONG now = GetTickCount64();
    if (m_shutdownIssuedAt != 0 && (m_shutdownForced || now - m_shutdownIssuedAt < kForceShutdownAfterMs)) return;

    // 일반 종료가 저장 안 한 문서 등으로 취소되면 1분 뒤 강제 종료한다.
    const bool force = m_shutdownIssuedAt != 0;
    m_shutdownIssuedAt = now;
    m_shutdownForced = force;
    SaveState();

    if (m_opt.dryRun) {
        LogEvent(_T("[%s] [테스트 모드] PC 종료 생략%s"), m_user.GetString(), force ? _T(" (강제)") : _T(""));
        return;
    }
    LogEvent(_T("[%s] PC 종료 실행%s"), m_user.GetString(), force ? _T(" (강제)") : _T(""));

    EnablePrivilege(SE_SHUTDOWN_NAME);
    const DWORD flags = SHUTDOWN_POWEROFF | (force ? SHUTDOWN_FORCE_OTHERS | SHUTDOWN_FORCE_SELF : 0);
    const DWORD result = InitiateShutdown(nullptr, const_cast<LPTSTR>(_T("사용 시간이 끝나 컴퓨터를 종료합니다.")), 0,
                                          flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER |
                                                     SHTDN_REASON_FLAG_PLANNED);
    if (result != ERROR_SUCCESS) LogEvent(_T("InitiateShutdown 실패 (%lu)"), result);
}

// ---------------------------------------------------------------------------
// 파이프 요청 처리 (작업 스레드에서 호출됨)

proto::Message Controller::Handle(const proto::Message& req, const PipeClientInfo& client) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const CString& cmd = req.Head();

    if (cmd == proto::kStatus) return HandleStatus(client);
    if (cmd == proto::kHistory) return HandleHistory(req, client);

    if (cmd == proto::kConfig) {
        proto::Message m = proto::Message::Ok();
        proto::ConfigToMessage(m_settings.schedule, m);
        for (const CString& u : m_settings.childUsers) m.Add(_T("child"), u);
        return m;
    }

    if (cmd == proto::kInitPassword) {
        if (m_settings.HasPassword()) return proto::Message::Error(proto::kErrDenied, _T("이미 비밀번호가 있습니다."));
        if (!client.isAdmin) return proto::Message::Error(proto::kErrDenied, _T("관리자 권한이 필요합니다."));
        const CString pw = req.Get(_T("newpw"));
        if (pw.GetLength() < PasswordHasher::kMinLength) return proto::Message::Error(proto::kErrInvalid);
        if (!PasswordHasher::Create(pw, m_settings.passwordSalt, m_settings.passwordHash,
                                    m_settings.passwordIterations) ||
            !SettingsStore::Save(m_settings)) {
            return proto::Message::Error(proto::kErrUnknown, _T("저장 실패"));
        }
        m_settingsStamp = SettingsStore::SettingsFileStamp();
        LogEvent(_T("비밀번호 최초 등록"));
        return proto::Message::Ok();
    }

    if (cmd == proto::kResetPassword) {
        if (!client.isAdmin) return proto::Message::Error(proto::kErrDenied, _T("관리자 권한이 필요합니다."));
        m_settings.passwordSalt.Empty();
        m_settings.passwordHash.Empty();
        m_settings.passwordIterations = 0;
        SettingsStore::Save(m_settings);
        m_settingsStamp = SettingsStore::SettingsFileStamp();
        LogEvent(_T("비밀번호 초기화 (관리자)"));
        return proto::Message::Ok();
    }

    return HandleProtected(req);
}

proto::Message Controller::HandleStatus(const PipeClientInfo& client) {
    proto::Message m = proto::Message::Ok();
    const bool tracked = !m_user.IsEmpty() && client.sessionId == m_sessionId;
    m.Set(_T("tracked"), tracked ? 1 : 0);
    m.Set(_T("hasPassword"), m_settings.HasPassword() ? 1 : 0);
    m.Set(_T("dryRun"), m_opt.dryRun ? 1 : 0);
    if (!tracked) return m;

    const cb::Status s = m_engine.GetStatus();
    const cb::ScheduleConfig& c = m_settings.schedule;
    m.Set(_T("user"), m_user);
    m.Set(_T("paused"), m_paused ? 1 : 0);
    m.Set(_T("phase"), static_cast<int64_t>(s.phase));
    m.Set(_T("toBreak"), s.msToBreak);
    m.Set(_T("breakRemaining"), s.msBreakRemaining);
    m.Set(_T("toShutdown"), s.msToShutdown);
    m.Set(_T("usedToday"), s.usedTodayMs);
    m.Set(_T("bonus"), m_engine.Save().bonusMs);
    m.Set(_T("breakTotal"), static_cast<int64_t>(c.breakMinutes) * 60 * 1000);
    m.Set(_T("breakWarnTotal"), static_cast<int64_t>(c.breakWarnSeconds) * 1000);
    m.Set(_T("shutdownWarnTotal"), static_cast<int64_t>(c.shutdownWarnSeconds) * 1000);
    return m;
}

proto::Message Controller::HandleHistory(const proto::Message& req, const PipeClientInfo& client) {
    CString user = req.Get(_T("user"));
    if (user.IsEmpty()) {
        if (!m_user.IsEmpty() && client.sessionId == m_sessionId)
            user = m_user;
        else if (!m_settings.childUsers.empty())
            user = m_settings.childUsers.front();
        else
            user = m_user;
    }

    proto::Message m = proto::Message::Ok();
    for (const CString& u : m_settings.childUsers) m.Add(_T("child"), u);
    m.Set(_T("user"), user);
    proto::ConfigToMessage(m_settings.schedule, m);
    if (user.IsEmpty()) return m;

    if (user.CompareNoCase(m_user) == 0) SaveState();  // 오늘 값을 최신으로
    for (const UsageRecord& r : SettingsStore::LoadHistory(user)) {
        CString line;
        line.Format(_T("%d,%lld,%lld"), r.date, r.usedMs, r.bonusMs);
        m.Add(_T("h"), line);
    }
    return m;
}

bool Controller::CheckPassword(const proto::Message& req, proto::Message& error) {
    const ULONGLONG now = GetTickCount64();
    if (now < m_lockedUntil) {
        error = proto::Message::Error(proto::kErrLocked);
        error.Set(_T("lockedMs"), static_cast<int64_t>(m_lockedUntil - now));
        return false;
    }
    if (PasswordHasher::Verify(req.Get(_T("pw")), m_settings.passwordSalt, m_settings.passwordHash,
                               m_settings.passwordIterations)) {
        m_failedAttempts = 0;
        return true;
    }

    ++m_failedAttempts;
    LogEvent(_T("비밀번호 틀림 (%d회, 명령 %s)"), m_failedAttempts, req.Head().GetString());
    if (m_failedAttempts >= kMaxFailedAttempts) {
        m_failedAttempts = 0;
        m_lockedUntil = now + kLockoutMs;
        LogEvent(_T("비밀번호 입력 5분 잠금"));
        error = proto::Message::Error(proto::kErrLocked);
        error.Set(_T("lockedMs"), static_cast<int64_t>(kLockoutMs));
    } else {
        error = proto::Message::Error(proto::kErrBadPassword);
    }
    return false;
}

proto::Message Controller::HandleProtected(const proto::Message& req) {
    const CString& cmd = req.Head();
    const bool known = cmd == proto::kVerify || cmd == proto::kSkipBreak || cmd == proto::kExtend ||
                       cmd == proto::kPause || cmd == proto::kResume || cmd == proto::kSetConfig ||
                       cmd == proto::kSetPassword;
    if (!known) return proto::Message::Error(proto::kErrUnknown, _T("알 수 없는 명령"));
    if (!m_settings.HasPassword()) return proto::Message::Error(proto::kErrDenied, _T("비밀번호가 등록되지 않았습니다."));

    proto::Message error;
    if (!CheckPassword(req, error)) return error;

    if (cmd == proto::kVerify) return proto::Message::Ok();

    if (cmd == proto::kSetConfig) {
        cb::ScheduleConfig c = proto::ConfigFromMessage(req);
        SettingsStore::Sanitize(c);
        m_settings.schedule = c;
        if (!SettingsStore::Save(m_settings)) return proto::Message::Error(proto::kErrUnknown, _T("저장 실패"));
        m_settingsStamp = SettingsStore::SettingsFileStamp();
        m_engine.SetConfig(c);
        if (!m_user.IsEmpty()) Refresh();
        LogEvent(_T("설정 변경: 사용 %d분/휴식 %d분(%s), 하루 %d분(%s), 종료 %02d:%02d(%s)"), c.useMinutes,
                 c.breakMinutes, c.breaksEnabled ? _T("켜짐") : _T("꺼짐"), c.dailyLimitMinutes,
                 c.dailyLimitEnabled ? _T("켜짐") : _T("꺼짐"), c.shutdownAtMinuteOfDay / 60,
                 c.shutdownAtMinuteOfDay % 60, c.shutdownAtEnabled ? _T("켜짐") : _T("꺼짐"));
        return proto::Message::Ok();
    }

    if (cmd == proto::kSetPassword) {
        const CString pw = req.Get(_T("newpw"));
        if (pw.GetLength() < PasswordHasher::kMinLength) return proto::Message::Error(proto::kErrInvalid);
        AppSettings updated = m_settings;
        if (!PasswordHasher::Create(pw, updated.passwordSalt, updated.passwordHash, updated.passwordIterations) ||
            !SettingsStore::Save(updated)) {
            return proto::Message::Error(proto::kErrUnknown, _T("저장 실패"));
        }
        m_settings = updated;
        m_settingsStamp = SettingsStore::SettingsFileStamp();
        LogEvent(_T("비밀번호 변경"));
        return proto::Message::Ok();
    }

    // 이하 명령은 현재 자녀 세션에 적용
    if (m_user.IsEmpty()) return proto::Message::Error(proto::kErrNoChild, _T("제한 중인 자녀 세션이 없습니다."));

    if (cmd == proto::kSkipBreak) {
        m_engine.SkipBreak();
        Refresh();
        LogEvent(_T("[%s] 부모가 휴식 해제"), m_user.GetString());
    } else if (cmd == proto::kExtend) {
        const int64_t minutes = req.GetInt(_T("minutes"));
        if (minutes < 1 || minutes > 240) return proto::Message::Error(proto::kErrInvalid);
        m_engine.Extend(minutes * 60 * 1000);
        Refresh();
        LogEvent(_T("[%s] 사용 시간 %lld분 연장"), m_user.GetString(), minutes);
    } else if (cmd == proto::kPause) {
        m_paused = true;
        m_engine.SetSuspended(true);
        LogEvent(_T("[%s] 오늘 제한 해제"), m_user.GetString());
    } else if (cmd == proto::kResume) {
        m_paused = false;
        m_engine.SetSuspended(false);
        Refresh();
        LogEvent(_T("[%s] 제한 다시 켜기"), m_user.GetString());
    }
    SaveState();
    return proto::Message::Ok();
}
