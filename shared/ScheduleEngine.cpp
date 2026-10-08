#include "ScheduleEngine.h"

#include <algorithm>

namespace cb {

namespace {
constexpr int64_t kSecondMs = 1000;
constexpr int64_t kMinuteMs = 60 * kSecondMs;
constexpr int64_t kDayMs = 24 * 60 * kMinuteMs;
}  // namespace

ScheduleEngine::ScheduleEngine(const ScheduleConfig& cfg) : cfg_(cfg) {}

void ScheduleEngine::SetConfig(const ScheduleConfig& cfg) {
    cfg_ = cfg;
    if (!cfg_.breaksEnabled) {
        st_.cycleUsedMs = 0;
        st_.breakRemainingMs = 0;
        if (phase_ == Phase::Break || phase_ == Phase::BreakWarning) phase_ = Phase::Using;
    }
}

void ScheduleEngine::Restore(const PersistentState& state, int64_t secondsOfDay) {
    st_ = state;
    st_.usedTodayMs = std::max<int64_t>(0, st_.usedTodayMs);
    st_.cycleUsedMs = std::max<int64_t>(0, st_.cycleUsedMs);
    st_.breakRemainingMs = std::max<int64_t>(0, st_.breakRemainingMs);
    st_.bonusMs = std::max<int64_t>(0, st_.bonusMs);
    if (!cfg_.breaksEnabled) {
        st_.cycleUsedMs = 0;
        st_.breakRemainingMs = 0;
    }
    phase_ = st_.breakRemainingMs > 0 ? Phase::Break : Phase::Using;
    Evaluate(secondsOfDay);
}

void ScheduleEngine::SetSuspended(bool suspended) {
    if (suspended_ == suspended) return;
    suspended_ = suspended;
    if (suspended_) {
        st_.breakRemainingMs = 0;
        st_.cycleUsedMs = 0;
        phase_ = Phase::Using;
        lastMsToShutdown_ = -1;
    }
}

void ScheduleEngine::Advance(int64_t deltaMs, int64_t secondsOfDay) {
    if (deltaMs < 0) deltaMs = 0;

    if (suspended_) {
        st_.usedTodayMs += deltaMs;
        return;
    }

    switch (phase_) {
    case Phase::Shutdown:
        return;
    case Phase::ShutdownWarning:
        shutdownCountdownMs_ -= deltaMs;
        st_.usedTodayMs += deltaMs;
        break;
    case Phase::Break:
        st_.breakRemainingMs -= deltaMs;
        if (cfg_.countBreakAsUsage) st_.usedTodayMs += deltaMs;
        if (st_.breakRemainingMs <= 0) {
            st_.breakRemainingMs = 0;
            st_.cycleUsedMs = 0;
            phase_ = Phase::Using;
        }
        break;
    default:
        st_.usedTodayMs += deltaMs;
        if (cfg_.breaksEnabled) st_.cycleUsedMs += deltaMs;
        break;
    }

    Evaluate(secondsOfDay);
}

void ScheduleEngine::SkipBreak() {
    st_.breakRemainingMs = 0;
    st_.cycleUsedMs = 0;
    if (phase_ == Phase::Break || phase_ == Phase::BreakWarning) phase_ = Phase::Using;
}

void ScheduleEngine::Extend(int64_t ms) {
    if (ms <= 0) return;
    st_.bonusMs += ms;
    // 경고/종료 단계였다면 다음 Evaluate 에서 다시 판단한다.
    if (phase_ == Phase::ShutdownWarning || phase_ == Phase::Shutdown) phase_ = Phase::Using;
}

void ScheduleEngine::ResetDay() {
    st_.usedTodayMs = 0;
    st_.bonusMs = 0;
    if (phase_ == Phase::ShutdownWarning || phase_ == Phase::Shutdown) phase_ = Phase::Using;
}

Status ScheduleEngine::GetStatus() const {
    Status s;
    s.phase = phase_;
    s.usedTodayMs = st_.usedTodayMs;
    if (suspended_) return s;  // 휴식·종료 없음
    if (cfg_.breaksEnabled && (phase_ == Phase::Using || phase_ == Phase::BreakWarning)) {
        s.msToBreak = std::max<int64_t>(0, cfg_.useMinutes * kMinuteMs - st_.cycleUsedMs);
    }
    s.msBreakRemaining = phase_ == Phase::Break ? st_.breakRemainingMs : 0;
    if (phase_ == Phase::ShutdownWarning) {
        s.msToShutdown = std::max<int64_t>(0, shutdownCountdownMs_);
    } else if (phase_ == Phase::Shutdown) {
        s.msToShutdown = 0;
    } else {
        s.msToShutdown = lastMsToShutdown_;
    }
    return s;
}

int64_t ScheduleEngine::ComputeMsToShutdown(int64_t secondsOfDay) const {
    int64_t best = -1;
    if (cfg_.dailyLimitEnabled) {
        int64_t remain = cfg_.dailyLimitMinutes * kMinuteMs + st_.bonusMs - st_.usedTodayMs;
        best = std::max<int64_t>(0, remain);
    }
    if (cfg_.shutdownAtEnabled) {
        int64_t target = cfg_.shutdownAtMinuteOfDay * kMinuteMs + st_.bonusMs;
        int64_t now = (secondsOfDay % (kDayMs / kSecondMs)) * kSecondMs;
        int64_t remain = std::max<int64_t>(0, target - now);
        best = best < 0 ? remain : std::min(best, remain);
    }
    return best;
}

void ScheduleEngine::Evaluate(int64_t secondsOfDay) {
    if (suspended_) {
        phase_ = Phase::Using;
        lastMsToShutdown_ = -1;
        return;
    }
    if (phase_ == Phase::Shutdown) return;

    const int64_t toShutdown = ComputeMsToShutdown(secondsOfDay);
    lastMsToShutdown_ = toShutdown;
    const int64_t warnMs = cfg_.shutdownWarnSeconds * kSecondMs;
    const bool shutdownImminent = toShutdown >= 0 && toShutdown <= warnMs;

    // 종료가 휴식보다 우선한다.
    if (phase_ == Phase::ShutdownWarning) {
        if (shutdownImminent) {
            if (shutdownCountdownMs_ <= 0) phase_ = Phase::Shutdown;
            return;
        }
        phase_ = Phase::Using;  // 설정 변경/연장으로 여유가 생김
    }
    if (shutdownImminent) {
        phase_ = Phase::ShutdownWarning;
        shutdownCountdownMs_ = std::max(toShutdown, kMinShutdownGraceMs);
        st_.breakRemainingMs = 0;
        return;
    }

    if (phase_ == Phase::Break) return;

    if (!cfg_.breaksEnabled || cfg_.useMinutes <= 0) {
        phase_ = Phase::Using;
        return;
    }

    const int64_t toBreak = cfg_.useMinutes * kMinuteMs - st_.cycleUsedMs;
    if (toBreak <= 0) {
        const int64_t breakMs = cfg_.breakMinutes * kMinuteMs;
        if (breakMs > 0) {
            st_.breakRemainingMs = breakMs;
            phase_ = Phase::Break;
        } else {
            st_.cycleUsedMs = 0;
            phase_ = Phase::Using;
        }
    } else if (toBreak <= cfg_.breakWarnSeconds * kSecondMs) {
        phase_ = Phase::BreakWarning;
    } else {
        phase_ = Phase::Using;
    }
}

}  // namespace cb
