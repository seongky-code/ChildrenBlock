#pragma once

#include "PipeServer.h"
#include "ScheduleEngine.h"
#include "SessionUtil.h"
#include "Settings.h"

struct ControllerOptions {
    bool asService = true;          // false: 콘솔 테스트 모드 (현재 사용자로 실행)
    bool dryRun = false;            // 실제로 PC 를 끄거나 세션을 잠그지 않음
    int speed = 1;                  // 사용 시간 배속 (테스트용)
    bool everyoneIsChild = false;   // 모든 계정을 자녀로 취급 (테스트용)
    bool protectTray = true;        // 트레이 프로세스를 사용자가 종료하지 못하게 함
};

// 서비스 본체: 1초마다 활성 세션을 확인해 자녀 계정이면 사용 시간을 계산하고,
// 트레이 앱을 실행·감시하며, 파이프로 들어오는 요청을 처리한다.
class Controller {
public:
    explicit Controller(const ControllerOptions& options);
    bool Start();
    void Stop();
    void Flush();  // Windows 종료 직전 상태 저장

private:
    void Run();
    void Tick();

    proto::Message Handle(const proto::Message& req, const PipeClientInfo& client);
    proto::Message HandleStatus(const PipeClientInfo& client);
    proto::Message HandleHistory(const proto::Message& req, const PipeClientInfo& client);
    proto::Message HandleProtected(const proto::Message& req);
    bool CheckPassword(const proto::Message& req, proto::Message& error);

    bool IsChild(const CString& user) const;
    void BeginTracking(const SessionInfo& session, int today, int64_t secondsOfDay);
    void EndTracking();
    void SaveState();
    void Refresh();  // 시간 진행 없이 상태 다시 계산
    void ReloadSettingsIfChanged();
    void WatchTray(const SessionInfo& session);
    void TerminateTrays();
    void DoShutdown();

    ControllerOptions m_opt;
    std::mutex m_mutex;

    AppSettings m_settings;
    ULONGLONG m_settingsStamp = 0;
    cb::ScheduleEngine m_engine;

    CString m_user;  // 현재 추적 중인 자녀 계정 (없으면 빈 문자열)
    DWORD m_sessionId = static_cast<DWORD>(-1);
    bool m_paused = false;
    int m_today = 0;

    ULONGLONG m_lastTick = 0;
    int m_ticksSinceSave = 0;
    int m_ticksSinceReload = 0;

    struct Tray {
        HANDLE process = nullptr;
        ULONGLONG nextLaunch = 0;
    };
    std::map<DWORD, Tray> m_trays;

    int m_failedAttempts = 0;
    ULONGLONG m_lockedUntil = 0;

    ULONGLONG m_shutdownIssuedAt = 0;
    bool m_shutdownForced = false;

    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;
    PipeServer m_server;
};
