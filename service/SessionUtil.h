#pragma once

struct SessionInfo {
    DWORD id = static_cast<DWORD>(-1);
    CString user;         // 로그온한 사용자 (없으면 빈 문자열)
    bool active = false;  // 화면에 연결된 세션
    bool locked = false;  // 잠금 화면
};

SessionInfo QuerySession(DWORD sessionId);
SessionInfo QueryActiveConsoleSession();

// 트레이 앱을 해당 세션 사용자 권한으로 실행한다.
//   asService = true : 서비스(SYSTEM)에서 WTSQueryUserToken 으로 사용자 세션에 실행
//   asService = false: 콘솔 테스트 모드, 현재 사용자로 실행
//   protect   = true : 사용자는 프로세스를 종료할 수 없도록 프로세스 DACL 제한
bool LaunchTray(DWORD sessionId, bool asService, bool protect, HANDLE& process);

bool EnablePrivilege(LPCTSTR name);
