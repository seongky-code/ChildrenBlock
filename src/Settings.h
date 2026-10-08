#pragma once

#include "ScheduleEngine.h"

// 설정 화면과 파일 로드 시 공통으로 쓰는 허용 범위
namespace Limits {
constexpr int kUseMinMin = 5, kUseMinMax = 180;
constexpr int kBreakMinMin = 1, kBreakMinMax = 60;
constexpr int kBreakWarnSecMin = 0, kBreakWarnSecMax = 300;
constexpr int kDailyLimitMinMin = 10, kDailyLimitMinMax = 720;
constexpr int kShutWarnMinMin = 1, kShutWarnMinMax = 10;
}  // namespace Limits

struct AppSettings {
    cb::ScheduleConfig schedule;
    CString passwordSalt;
    CString passwordHash;
    int passwordIterations = 0;

    bool HasPassword() const {
        return !passwordSalt.IsEmpty() && !passwordHash.IsEmpty() && passwordIterations > 0;
    }
};

struct SavedState {
    int date = 0;  // YYYYMMDD
    cb::PersistentState engine;
};

// 날짜별 사용 기록 (state.ini [History] 에 "YYYYMMDD=사용ms,연장ms" 로 저장)
struct UsageRecord {
    int date = 0;
    int64_t usedMs = 0;
    int64_t bonusMs = 0;
};

constexpr size_t kHistoryKeepDays = 400;

namespace SettingsStore {
void Load(AppSettings& settings);
bool Save(const AppSettings& settings);
// settings.ini 마지막 수정 시각 (없으면 0). 다른 프로세스의 변경 감지용
ULONGLONG SettingsFileStamp();
bool LoadState(SavedState& state);
bool SaveState(const SavedState& state);  // 해당 날짜의 사용 기록도 함께 갱신

std::vector<UsageRecord> LoadHistory();  // 날짜 오름차순
void PruneHistory(size_t keepDays);
}  // namespace SettingsStore
