#pragma once

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_UNLOCK_REQUEST = WM_APP + 2;

constexpr LPCTSTR kAppTitle = _T("ChildrenBlock");

// "1:05:09" 또는 "5:09" 형식. 카운트다운이 0 이 되기 전까지 1초 이상으로 보이도록 올림한다.
CString FormatDuration(int64_t ms);

// "1:05" (시:분, 내림)
CString FormatHoursMinutesShort(int64_t ms);

// "1시간 5분" / "45분"
CString FormatHoursMinutesLong(int64_t ms);

// %ProgramData%\ChildrenBlock (없으면 생성)
CString GetDataDir();

void LogEvent(LPCTSTR format, ...);

// 96 DPI 기준 값을 현재 시스템 DPI 에 맞게 변환
int ScaleForDpi(int value);

// 대화 상자 제목 표시줄/작업 표시줄에 프로그램 아이콘 적용
void ApplyAppIcon(CWnd* wnd);
