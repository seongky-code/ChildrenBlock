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
    std::vector<CString> childUsers;  // 제한을 적용할 Windows 계정 이름
    CString passwordSalt;
    CString passwordHash;
    int passwordIterations = 0;

    bool HasPassword() const {
        return !passwordSalt.IsEmpty() && !passwordHash.IsEmpty() && passwordIterations > 0;
    }
    bool IsChild(const CString& user) const;
};

// 자녀 계정별 상태 (state-<계정>.ini)
struct SavedState {
    int date = 0;        // YYYYMMDD
    int pausedDate = 0;  // 이 날짜에는 "오늘 제한 해제" 상태
    cb::PersistentState engine;
};

// 날짜별 사용 기록 (state-<계정>.ini [History] 에 "YYYYMMDD=사용ms,연장ms" 로 저장)
struct UsageRecord {
    int date = 0;
    int64_t usedMs = 0;
    int64_t bonusMs = 0;
};

constexpr size_t kHistoryKeepDays = 400;

namespace SettingsStore {
void Load(AppSettings& settings);
bool Save(const AppSettings& settings);
void Sanitize(cb::ScheduleConfig& config);

// settings.ini 마지막 수정 시각 (없으면 0). 다른 프로세스의 변경 감지용
ULONGLONG SettingsFileStamp();

bool LoadState(const CString& user, SavedState& state);
bool SaveState(const CString& user, const SavedState& state);  // 해당 날짜의 사용 기록도 함께 갱신

std::vector<UsageRecord> LoadHistory(const CString& user);  // 날짜 오름차순
void PruneHistory(const CString& user, size_t keepDays);
}  // namespace SettingsStore
