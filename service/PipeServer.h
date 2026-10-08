#pragma once

#include "Protocol.h"

struct PipeClientInfo {
    DWORD sessionId = static_cast<DWORD>(-1);
    bool isAdmin = false;  // 관리자 권한(UAC 승격)으로 실행된 클라이언트
};

// 메시지 모드 Named Pipe 서버. 연결마다 작업 스레드를 띄우고, 모든 입출력에 시간 제한을 둔다.
class PipeServer {
public:
    using Handler = std::function<proto::Message(const proto::Message&, const PipeClientInfo&)>;

    ~PipeServer() { Stop(); }
    bool Start(const CString& name, Handler handler);
    void Stop();

private:
    void AcceptLoop(HANDLE firstInstance);
    void Serve(HANDLE pipe);
    HANDLE CreateInstance(bool first);

    CString m_name;
    Handler m_handler;
    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;
    std::atomic<int> m_active{0};
    PSECURITY_DESCRIPTOR m_sd = nullptr;
};
