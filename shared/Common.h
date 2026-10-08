#pragma once

// 서비스와 트레이 앱이 함께 쓰는 유틸리티 (UI 비의존)

constexpr LPCTSTR kAppTitle = _T("ChildrenBlock");
constexpr LPCTSTR kServiceName = _T("ChildrenBlock");
constexpr LPCTSTR kTrayExeName = _T("ChildrenBlock.exe");

// 트레이 앱 종료 코드: 같은 세션에 이미 실행 중이라 바로 끝남 (서비스가 강제 종료로 오인하지 않도록)
constexpr int kExitAlreadyRunning = 2;

// "1:05:09" 또는 "5:09" 형식. 카운트다운이 0 이 되기 전까지 1초 이상으로 보이도록 올림한다.
CString FormatDuration(int64_t ms);

// "1:05" (시:분, 내림)
CString FormatHoursMinutesShort(int64_t ms);

// "1시간 5분" / "45분"
CString FormatHoursMinutesLong(int64_t ms);

// %ProgramData%\ChildrenBlock (없으면 생성)
CString GetDataDir();

// 현재 실행 파일이 있는 폴더
CString GetModuleDir();

// log.txt 에 한 줄 기록 (스레드 안전). 권한이 없으면 조용히 무시한다.
void LogEvent(LPCTSTR format, ...);

int TodayAsInt(const SYSTEMTIME& st);         // YYYYMMDD
int64_t SecondsOfDay(const SYSTEMTIME& st);   // 0~86399
