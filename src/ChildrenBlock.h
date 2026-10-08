#pragma once

#include "Settings.h"
#include "TrayWnd.h"

class CChildrenBlockApp : public CWinApp {
public:
    virtual BOOL InitInstance();
    virtual int ExitInstance();

private:
    struct CommandLine {
        AppOptions options;
        bool resetPassword = false;
        bool setup = false;
        bool history = false;
    };

    static CommandLine ParseCommandLine();
    bool EnsurePassword();
    void RunSetup();
    void ResetPassword();

    AppSettings m_settings;
    HANDLE m_hMutex = nullptr;
};

extern CChildrenBlockApp theApp;
