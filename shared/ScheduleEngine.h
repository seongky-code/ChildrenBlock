#pragma once

// 사용/휴식/종료 판단 로직. UI·Windows API에 의존하지 않아 단위 테스트가 가능하다.

#include <cstdint>

namespace cb {

struct ScheduleConfig {
    bool breaksEnabled = true;
    int useMinutes = 30;
    int breakMinutes = 5;
    int breakWarnSeconds = 60;

    bool dailyLimitEnabled = true;
    int dailyLimitMinutes = 120;

    bool shutdownAtEnabled = false;
    int shutdownAtMinuteOfDay = 21 * 60;

    int shutdownWarnSeconds = 300;
    bool countBreakAsUsage = false;
};

enum class Phase { Using, BreakWarning, Break, ShutdownWarning, Shutdown };

// 재부팅 후에도 복원해야 하는 값
struct PersistentState {
    int64_t usedTodayMs = 0;
    int64_t cycleUsedMs = 0;
    int64_t breakRemainingMs = 0;  // > 0 이면 휴식 중
    int64_t bonusMs = 0;           // 부모가 연장해 준 시간
};

struct Status {
    Phase phase = Phase::Using;
    int64_t msToBreak = -1;         // 휴식까지 남은 시간 (-1: 해당 없음)
    int64_t msBreakRemaining = 0;   // 휴식 남은 시간
    int64_t msToShutdown = -1;      // 종료까지 남은 시간 (-1: 제한 없음)
    int64_t usedTodayMs = 0;
};

class ScheduleEngine {
public:
    // 이미 사용 한도를 넘긴 상태로 시작해도 최소 이만큼은 경고 후 종료한다.
    static constexpr int64_t kMinShutdownGraceMs = 60 * 1000;

    explicit ScheduleEngine(const ScheduleConfig& cfg = ScheduleConfig());

    void SetConfig(const ScheduleConfig& cfg);
    const ScheduleConfig& Config() const { return cfg_; }

    void Restore(const PersistentState& state, int64_t secondsOfDay);
    PersistentState Save() const { return st_; }

    // deltaMs: 단조 시계 기준 경과 시간, secondsOfDay: 현재 현지 시각(0~86399)
    void Advance(int64_t deltaMs, int64_t secondsOfDay);

    void SkipBreak();
    void Extend(int64_t ms);
    void ResetDay();

    // 부모가 "오늘 제한 해제" 한 상태. 사용 시간은 계속 기록하지만 휴식·종료는 하지 않는다.
    void SetSuspended(bool suspended);
    bool IsSuspended() const { return suspended_; }

    Phase GetPhase() const { return phase_; }
    Status GetStatus() const;

private:
    int64_t ComputeMsToShutdown(int64_t secondsOfDay) const;
    void Evaluate(int64_t secondsOfDay);

    ScheduleConfig cfg_;
    PersistentState st_;
    Phase phase_ = Phase::Using;
    bool suspended_ = false;
    int64_t shutdownCountdownMs_ = 0;
    int64_t lastMsToShutdown_ = -1;
};

}  // namespace cb
