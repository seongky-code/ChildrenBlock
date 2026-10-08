// ScheduleEngine 단위 테스트. 가짜 시간을 1초 단위로 진행시켜 실제 타이머 루프와 같은 방식으로 검증한다.

#include "ScheduleEngine.h"

#include <cstdio>
#include <functional>
#include <vector>

using namespace cb;

namespace {

constexpr int64_t kSec = 1000;
constexpr int64_t kMin = 60 * kSec;

int g_failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

struct Sim {
    ScheduleEngine engine;
    int64_t clockMs;

    Sim(const ScheduleConfig& cfg, int64_t startSecondsOfDay = 10 * 3600)
        : engine(cfg), clockMs(startSecondsOfDay * kSec) {
        engine.Restore(PersistentState(), Sod());
    }

    int64_t Sod() const { return (clockMs / kSec) % 86400; }

    void Run(int64_t ms) {
        for (int64_t t = 0; t < ms; t += kSec) {
            clockMs += kSec;
            engine.Advance(kSec, Sod());
        }
    }

    Phase P() const { return engine.GetPhase(); }
};

ScheduleConfig BreaksOnly() {
    ScheduleConfig c;
    c.breaksEnabled = true;
    c.useMinutes = 30;
    c.breakMinutes = 5;
    c.breakWarnSeconds = 60;
    c.dailyLimitEnabled = false;
    c.shutdownAtEnabled = false;
    return c;
}

ScheduleConfig LimitOnly(int limitMinutes) {
    ScheduleConfig c;
    c.breaksEnabled = false;
    c.dailyLimitEnabled = true;
    c.dailyLimitMinutes = limitMinutes;
    c.shutdownAtEnabled = false;
    c.shutdownWarnSeconds = 300;
    return c;
}

void TestUseBreakCycle() {
    Sim s(BreaksOnly());
    s.Run(28 * kMin);
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToBreak == 2 * kMin);

    s.Run(1 * kMin);
    CHECK(s.P() == Phase::BreakWarning);
    CHECK(s.engine.GetStatus().msToBreak == 60 * kSec);

    s.Run(59 * kSec);
    CHECK(s.P() == Phase::BreakWarning);
    s.Run(1 * kSec);
    CHECK(s.P() == Phase::Break);
    CHECK(s.engine.GetStatus().msBreakRemaining == 5 * kMin);

    s.Run(5 * kMin - kSec);
    CHECK(s.P() == Phase::Break);
    s.Run(1 * kSec);
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToBreak == 30 * kMin);

    // 두 번째 주기도 동일하게 동작
    s.Run(30 * kMin);
    CHECK(s.P() == Phase::Break);
}

void TestBreakNotCountedByDefault() {
    Sim s(BreaksOnly());
    s.Run(35 * kMin);  // 30분 사용 + 5분 휴식
    CHECK(s.engine.GetStatus().usedTodayMs == 30 * kMin);

    ScheduleConfig c = BreaksOnly();
    c.countBreakAsUsage = true;
    Sim s2(c);
    s2.Run(35 * kMin);
    CHECK(s2.engine.GetStatus().usedTodayMs == 35 * kMin);
}

void TestDailyLimitShutdown() {
    Sim s(LimitOnly(60));
    s.Run(54 * kMin);
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToShutdown == 6 * kMin);

    s.Run(1 * kMin);
    CHECK(s.P() == Phase::ShutdownWarning);
    CHECK(s.engine.GetStatus().msToShutdown == 5 * kMin);

    s.Run(5 * kMin - kSec);
    CHECK(s.P() == Phase::ShutdownWarning);
    s.Run(1 * kSec);
    CHECK(s.P() == Phase::Shutdown);

    // 종료 단계에서는 더 이상 진행하지 않는다.
    s.Run(10 * kMin);
    CHECK(s.P() == Phase::Shutdown);
}

void TestShutdownHasPriorityOverBreak() {
    ScheduleConfig c = BreaksOnly();
    c.dailyLimitEnabled = true;
    c.dailyLimitMinutes = 30;
    c.shutdownWarnSeconds = 300;
    Sim s(c);
    s.Run(25 * kMin);
    CHECK(s.P() == Phase::ShutdownWarning);
    s.Run(5 * kMin);
    CHECK(s.P() == Phase::Shutdown);
}

void TestShutdownDuringBreak() {
    ScheduleConfig c = BreaksOnly();
    c.countBreakAsUsage = true;
    c.dailyLimitEnabled = true;
    c.dailyLimitMinutes = 33;
    c.shutdownWarnSeconds = 60;
    Sim s(c);
    s.Run(30 * kMin);
    CHECK(s.P() == Phase::Break);
    s.Run(2 * kMin);
    CHECK(s.P() == Phase::ShutdownWarning);
    CHECK(s.engine.GetStatus().msBreakRemaining == 0);
}

void TestRestoreOverLimitGetsGrace() {
    ScheduleEngine e(LimitOnly(60));
    PersistentState st;
    st.usedTodayMs = 70 * kMin;
    e.Restore(st, 10 * 3600);
    CHECK(e.GetPhase() == Phase::ShutdownWarning);
    CHECK(e.GetStatus().msToShutdown == ScheduleEngine::kMinShutdownGraceMs);

    for (int i = 0; i < 59; ++i) e.Advance(kSec, 10 * 3600 + i);
    CHECK(e.GetPhase() == Phase::ShutdownWarning);
    e.Advance(kSec, 10 * 3600 + 60);
    CHECK(e.GetPhase() == Phase::Shutdown);
}

void TestShutdownAtClock() {
    ScheduleConfig c;
    c.breaksEnabled = false;
    c.dailyLimitEnabled = false;
    c.shutdownAtEnabled = true;
    c.shutdownAtMinuteOfDay = 21 * 60;
    c.shutdownWarnSeconds = 300;

    Sim s(c, 20 * 3600 + 50 * 60);  // 20:50 시작
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToShutdown == 10 * kMin);
    s.Run(5 * kMin);  // 20:55
    CHECK(s.P() == Phase::ShutdownWarning);
    s.Run(5 * kMin);  // 21:00
    CHECK(s.P() == Phase::Shutdown);

    // 종료 시각 이후에 켜면 유예 시간 뒤 종료
    Sim late(c, 22 * 3600);
    CHECK(late.P() == Phase::ShutdownWarning);
    CHECK(late.engine.GetStatus().msToShutdown == ScheduleEngine::kMinShutdownGraceMs);
}

void TestEarliestOfLimitAndClock() {
    ScheduleConfig c;
    c.breaksEnabled = false;
    c.dailyLimitEnabled = true;
    c.dailyLimitMinutes = 120;
    c.shutdownAtEnabled = true;
    c.shutdownAtMinuteOfDay = 21 * 60;
    Sim s(c, 20 * 3600);  // 20:00, 한도는 2시간 남았지만 21:00 이 먼저
    CHECK(s.engine.GetStatus().msToShutdown == 60 * kMin);
}

void TestExtendLeavesShutdownWarning() {
    Sim s(LimitOnly(60));
    s.Run(56 * kMin);
    CHECK(s.P() == Phase::ShutdownWarning);
    s.engine.Extend(30 * kMin);
    s.engine.Advance(0, s.Sod());
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToShutdown == 34 * kMin);

    // 종료 단계에서도 연장 가능
    s.Run(34 * kMin);
    CHECK(s.P() == Phase::Shutdown);
    s.engine.Extend(15 * kMin);
    s.engine.Advance(0, s.Sod());
    CHECK(s.P() == Phase::Using);
}

void TestSkipBreak() {
    Sim s(BreaksOnly());
    s.Run(31 * kMin);
    CHECK(s.P() == Phase::Break);
    s.engine.SkipBreak();
    s.engine.Advance(0, s.Sod());
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToBreak == 30 * kMin);
}

void TestRestoreDuringBreak() {
    ScheduleEngine e(BreaksOnly());
    PersistentState st;
    st.breakRemainingMs = 3 * kMin;
    st.cycleUsedMs = 30 * kMin;
    e.Restore(st, 10 * 3600);
    CHECK(e.GetPhase() == Phase::Break);
    CHECK(e.GetStatus().msBreakRemaining == 3 * kMin);
}

void TestResetDay() {
    Sim s(LimitOnly(60));
    s.Run(58 * kMin);
    CHECK(s.P() == Phase::ShutdownWarning);
    s.engine.ResetDay();
    s.engine.Advance(0, s.Sod());
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().usedTodayMs == 0);
    CHECK(s.engine.GetStatus().msToShutdown == 60 * kMin);
}

void TestDisableBreaksWhileInBreak() {
    Sim s(BreaksOnly());
    s.Run(31 * kMin);
    CHECK(s.P() == Phase::Break);
    ScheduleConfig c = BreaksOnly();
    c.breaksEnabled = false;
    s.engine.SetConfig(c);
    s.engine.Advance(0, s.Sod());
    CHECK(s.P() == Phase::Using);
    CHECK(s.engine.GetStatus().msToBreak == -1);
}

}  // namespace

int main() {
    struct Case { const char* name; std::function<void()> fn; };
    std::vector<Case> cases = {
        {"UseBreakCycle", TestUseBreakCycle},
        {"BreakNotCountedByDefault", TestBreakNotCountedByDefault},
        {"DailyLimitShutdown", TestDailyLimitShutdown},
        {"ShutdownHasPriorityOverBreak", TestShutdownHasPriorityOverBreak},
        {"ShutdownDuringBreak", TestShutdownDuringBreak},
        {"RestoreOverLimitGetsGrace", TestRestoreOverLimitGetsGrace},
        {"ShutdownAtClock", TestShutdownAtClock},
        {"EarliestOfLimitAndClock", TestEarliestOfLimitAndClock},
        {"ExtendLeavesShutdownWarning", TestExtendLeavesShutdownWarning},
        {"SkipBreak", TestSkipBreak},
        {"RestoreDuringBreak", TestRestoreDuringBreak},
        {"ResetDay", TestResetDay},
        {"DisableBreaksWhileInBreak", TestDisableBreaksWhileInBreak},
    };

    for (const auto& c : cases) {
        int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("\n%zu tests, %d failed checks\n", cases.size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}
