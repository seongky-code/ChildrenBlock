#include "pch.h"
#include "SessionUtil.h"

#include "Common.h"

SessionInfo QuerySession(DWORD sessionId) {
    SessionInfo info;
    info.id = sessionId;
    if (sessionId == static_cast<DWORD>(-1)) return info;

    LPTSTR buffer = nullptr;
    DWORD bytes = 0;
    if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSSessionInfoEx, &buffer, &bytes) &&
        buffer) {
        const auto* ex = reinterpret_cast<const WTSINFOEX*>(buffer);
        if (ex->Level == 1) {
            const WTSINFOEX_LEVEL1& l1 = ex->Data.WTSInfoExLevel1;
            info.user = l1.UserName;
            info.active = l1.SessionState == WTSActive;
            info.locked = l1.SessionFlags == WTS_SESSIONSTATE_LOCK;
        }
        WTSFreeMemory(buffer);
    }
    return info;
}

SessionInfo QueryActiveConsoleSession() { return QuerySession(WTSGetActiveConsoleSessionId()); }

bool EnablePrivilege(LPCTSTR name) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValue(nullptr, name, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

bool LaunchTray(DWORD sessionId, bool asService, bool protect, HANDLE& process) {
    process = nullptr;
    const CString exe = GetModuleDir() + _T("\\") + kTrayExeName;
    CString cmd = _T("\"") + exe + _T("\" /agent");

    // 사용자(IU)와 프로세스 소유자(OW)에게는 조회·대기 권한만 준다 → 작업 관리자/taskkill 로 종료 불가
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
    if (protect &&
        ConvertStringSecurityDescriptorToSecurityDescriptor(
            _T("D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x101400;;;OW)(A;;0x101400;;;IU)"), SDDL_REVISION_1, &sd, nullptr)) {
        sa.lpSecurityDescriptor = sd;
    }

    STARTUPINFO si = {sizeof(si)};
    si.lpDesktop = const_cast<LPTSTR>(_T("winsta0\\default"));
    PROCESS_INFORMATION pi = {};
    BOOL ok = FALSE;

    if (asService) {
        HANDLE userToken = nullptr;
        if (WTSQueryUserToken(sessionId, &userToken)) {
            LPVOID env = nullptr;
            CreateEnvironmentBlock(&env, userToken, FALSE);
            ok = CreateProcessAsUser(userToken, exe, cmd.GetBuffer(), &sa, nullptr, FALSE,
                                     CREATE_UNICODE_ENVIRONMENT | NORMAL_PRIORITY_CLASS, env, GetModuleDir(), &si,
                                     &pi);
            cmd.ReleaseBuffer();
            if (env) DestroyEnvironmentBlock(env);
            CloseHandle(userToken);
        }
    } else {
        ok = CreateProcess(exe, cmd.GetBuffer(), &sa, nullptr, FALSE, NORMAL_PRIORITY_CLASS, nullptr, GetModuleDir(),
                           &si, &pi);
        cmd.ReleaseBuffer();
    }
    const DWORD err = GetLastError();
    if (sd) LocalFree(sd);

    if (!ok) {
        LogEvent(_T("트레이 앱 실행 실패 (세션 %lu, 오류 %lu)"), sessionId, err);
        return false;
    }
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    return true;
}
