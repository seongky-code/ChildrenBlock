#include "pch.h"
#include "PipeServer.h"

#include "Common.h"

namespace {

constexpr DWORD kBufferSize = 64 * 1024;
constexpr DWORD kIoTimeoutMs = 3000;
constexpr int kMaxConnections = 32;

// 겹친 I/O 를 시간 제한 안에 끝낸다. 시간이 넘으면 취소하고 false.
bool WaitIo(HANDLE pipe, OVERLAPPED& ov, BOOL started, DWORD& transferred, DWORD& error) {
    error = started ? ERROR_SUCCESS : GetLastError();
    if (!started && error == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, kIoTimeoutMs) != WAIT_OBJECT_0) {
            CancelIo(pipe);
            WaitForSingleObject(ov.hEvent, 1000);
            error = WAIT_TIMEOUT;
            return false;
        }
        started = GetOverlappedResult(pipe, &ov, &transferred, FALSE);
        error = started ? ERROR_SUCCESS : GetLastError();
    }
    return started != FALSE;
}

bool IsClientAdmin(HANDLE pipe) {
    if (!ImpersonateNamedPipeClient(pipe)) return false;
    HANDLE token = nullptr;
    BOOL member = FALSE;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        BYTE sid[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof(sid);
        if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &size))
            CheckTokenMembership(token, sid, &member);
        CloseHandle(token);
    }
    RevertToSelf();
    return member != FALSE;
}

}  // namespace

bool PipeServer::Start(const CString& name, Handler handler) {
    m_name = name;
    m_handler = std::move(handler);

    // SYSTEM·관리자·만든 프로세스(OW): 전체
    // 대화형 사용자(IU): 읽기 + 데이터 쓰기 + 특성 쓰기만 (인스턴스 생성 권한이 없어 가짜 서버를 만들 수 없음)
    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
            _T("D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)(A;;0x12018B;;;IU)"), SDDL_REVISION_1, &m_sd, nullptr)) {
        return false;
    }

    m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    HANDLE first = CreateInstance(true);
    if (first == INVALID_HANDLE_VALUE) {
        LogEvent(_T("파이프 생성 실패 (%lu) - 이미 같은 이름의 파이프가 있습니다"), GetLastError());
        return false;
    }
    m_thread = std::thread(&PipeServer::AcceptLoop, this, first);
    return true;
}

void PipeServer::Stop() {
    if (!m_stopEvent) return;
    SetEvent(m_stopEvent);
    if (m_thread.joinable()) m_thread.join();
    for (int i = 0; i < 100 && m_active > 0; ++i) Sleep(50);  // 작업 스레드는 시간 제한 안에 끝난다
    CloseHandle(m_stopEvent);
    m_stopEvent = nullptr;
    if (m_sd) {
        LocalFree(m_sd);
        m_sd = nullptr;
    }
}

HANDLE PipeServer::CreateInstance(bool first) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), m_sd, FALSE};
    return CreateNamedPipe(m_name,
                           PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                           PIPE_UNLIMITED_INSTANCES, kBufferSize, kBufferSize, 0, &sa);
}

void PipeServer::AcceptLoop(HANDLE pipe) {
    // 다음 인스턴스는 연결을 넘기기 전에 만들어 두어 이름이 비는 순간이 없게 한다.
    while (pipe != INVALID_HANDLE_VALUE) {
        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD err = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && err == ERROR_IO_PENDING) {
            HANDLE waits[] = {ov.hEvent, m_stopEvent};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIo(pipe);
                WaitForSingleObject(ov.hEvent, 1000);
                CloseHandle(ov.hEvent);
                CloseHandle(pipe);
                return;
            }
            DWORD dummy = 0;
            connected = GetOverlappedResult(pipe, &ov, &dummy, FALSE);
        } else if (!connected && err == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        CloseHandle(ov.hEvent);

        HANDLE next = CreateInstance(false);
        if (connected && m_active < kMaxConnections) {
            ++m_active;
            std::thread([this, pipe] {
                Serve(pipe);
                --m_active;
            }).detach();
        } else {
            CloseHandle(pipe);
        }
        pipe = next;
        if (WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0) {
            if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
            return;
        }
    }
    LogEvent(_T("파이프 인스턴스 생성 실패 (%lu)"), GetLastError());
}

void PipeServer::Serve(HANDLE pipe) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    std::vector<char> buf(kBufferSize);
    std::string request;
    DWORD transferred = 0, err = 0;

    // 요청 읽기
    for (;;) {
        ResetEvent(ov.hEvent);
        transferred = 0;
        const BOOL started = ReadFile(pipe, buf.data(), kBufferSize, &transferred, &ov);
        const bool ok = WaitIo(pipe, ov, started, transferred, err);
        request.append(buf.data(), transferred);
        if (ok) break;
        if (err != ERROR_MORE_DATA || request.size() > 1024 * 1024) {
            request.clear();
            break;
        }
    }

    proto::Message req;
    if (!request.empty() && proto::Message::Decode(request.data(), request.size(), req)) {
        PipeClientInfo client;
        ULONG session = 0;
        if (GetNamedPipeClientSessionId(pipe, &session)) client.sessionId = session;
        client.isAdmin = IsClientAdmin(pipe);

        proto::Message resp;
        try {
            resp = m_handler(req, client);
        } catch (...) {
            resp = proto::Message::Error(proto::kErrUnknown, _T("internal error"));
        }

        const std::string out = resp.Encode();
        ResetEvent(ov.hEvent);
        const BOOL started = WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &transferred, &ov);
        if (WaitIo(pipe, ov, started, transferred, err)) {
            // 클라이언트가 응답을 다 읽고 닫을 때까지 기다린다 (Disconnect 는 읽지 않은 데이터를 버림).
            ResetEvent(ov.hEvent);
            const BOOL waitClose = ReadFile(pipe, buf.data(), kBufferSize, &transferred, &ov);
            WaitIo(pipe, ov, waitClose, transferred, err);
        }
    }

    CloseHandle(ov.hEvent);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}
