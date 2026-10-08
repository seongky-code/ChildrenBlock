#pragma once

#include "TrayWnd.h"

class CChildrenBlockApp : public CWinApp {
public:
    virtual BOOL InitInstance();
    virtual int ExitInstance();

private:
    struct CommandLine {
        bool agent = false;  // 서비스가 실행한 트레이
        bool setup = false;
        bool history = false;
        bool resetPassword = false;
    };

    static CommandLine ParseCommandLine();
    void RunSetup();
    void ResetPassword();

    HANDLE m_hMutex = nullptr;
    int m_exitCode = 0;
};

extern CChildrenBlockApp theApp;
