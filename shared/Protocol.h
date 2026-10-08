#pragma once

// 서비스 ↔ 트레이/설정 앱 Named Pipe 프로토콜
//
// 요청/응답 모두 UTF-8 텍스트 한 메시지:
//   첫 줄 = 명령(요청) 또는 OK / ERR(응답)
//   다음 줄부터 key=value (같은 key 반복 가능)
//
// 비밀번호가 필요한 명령은 pw=... 를 함께 보낸다. 실패 시 ERR + code=badpw|locked|...

#include "ScheduleEngine.h"

namespace proto {

// 명령
constexpr LPCTSTR kStatus = _T("STATUS");             // 상태 (요청한 세션 기준)
constexpr LPCTSTR kConfig = _T("CONFIG");             // 설정 조회
constexpr LPCTSTR kHistory = _T("HISTORY");           // user=(선택) 사용 기록
constexpr LPCTSTR kVerify = _T("VERIFY");             // pw
constexpr LPCTSTR kSkipBreak = _T("SKIPBREAK");       // pw
constexpr LPCTSTR kExtend = _T("EXTEND");             // pw, minutes
constexpr LPCTSTR kPause = _T("PAUSE");               // pw  오늘 제한 해제
constexpr LPCTSTR kResume = _T("RESUME");             // pw  제한 다시 켜기
constexpr LPCTSTR kSetConfig = _T("SETCONFIG");       // pw + 설정 값
constexpr LPCTSTR kSetPassword = _T("SETPASSWORD");   // pw, newpw
constexpr LPCTSTR kInitPassword = _T("INITPASSWORD"); // newpw (비밀번호가 없을 때, 관리자만)
constexpr LPCTSTR kResetPassword = _T("RESETPASSWORD");  // (관리자만)

// 오류 코드
constexpr LPCTSTR kErrBadPassword = _T("badpw");
constexpr LPCTSTR kErrLocked = _T("locked");
constexpr LPCTSTR kErrDenied = _T("denied");
constexpr LPCTSTR kErrInvalid = _T("invalid");
constexpr LPCTSTR kErrNoChild = _T("nochild");
constexpr LPCTSTR kErrUnknown = _T("unknown");

CString PipeName();

class Message {
public:
    Message() = default;
    explicit Message(LPCTSTR head) : m_head(head) {}

    const CString& Head() const { return m_head; }
    bool IsOk() const { return m_head == _T("OK"); }

    void Set(LPCTSTR key, const CString& value);
    void Set(LPCTSTR key, LPCTSTR value) { Set(key, CString(value)); }
    void Set(LPCTSTR key, int64_t value);
    void Add(LPCTSTR key, const CString& value);  // 중복 허용

    bool Has(LPCTSTR key) const;
    CString Get(LPCTSTR key, LPCTSTR def = _T("")) const;
    int64_t GetInt(LPCTSTR key, int64_t def = 0) const;
    std::vector<CString> GetAll(LPCTSTR key) const;

    std::string Encode() const;
    static bool Decode(const char* data, size_t size, Message& out);

    static Message Ok() { return Message(_T("OK")); }
    static Message Error(LPCTSTR code, LPCTSTR text = _T(""));

private:
    CString m_head;
    std::vector<std::pair<CString, CString>> m_fields;
};

void ConfigToMessage(const cb::ScheduleConfig& config, Message& msg);
cb::ScheduleConfig ConfigFromMessage(const Message& msg);

// STATUS 응답을 해석한 값
struct Status {
    bool connected = false;    // 서비스에 연결됨
    bool tracked = false;      // 요청한 세션이 제한 대상 자녀 세션
    bool hasPassword = false;
    bool paused = false;       // 오늘 제한 해제
    bool dryRun = false;
    CString user;
    cb::Status engine;
    int64_t bonusMs = 0;
    int64_t breakTotalMs = 0;
    int64_t breakWarnTotalMs = 0;
    int64_t shutdownWarnTotalMs = 0;
};
Status ParseStatus(const Message& msg);

// 서비스에 요청을 보내고 응답을 받는다. 서비스에 연결하지 못하면 false.
// Release 빌드에서는 파이프 서버가 세션 0(서비스)인지 확인해 가짜 서버에 비밀번호를 보내지 않는다.
bool Transact(const Message& request, Message& response, DWORD timeoutMs = 3000);

}  // namespace proto
