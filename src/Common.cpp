#include "pch.h"
#include "Common.h"

#include "resource.h"

#include <cstdio>

void ApplyAppIcon(CWnd* wnd) {
    const HINSTANCE res = AfxGetResourceHandle();
    auto load = [res](int cx, int cy) {
        return static_cast<HICON>(LoadImage(res, MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON, cx, cy, LR_SHARED));
    };
    wnd->SetIcon(load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)), TRUE);
    wnd->SetIcon(load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)), FALSE);
}

CString FormatDuration(int64_t ms) {
    if (ms < 0) ms = 0;
    const int64_t total = (ms + 999) / 1000;
    const int64_t h = total / 3600;
    const int64_t m = (total % 3600) / 60;
    const int64_t s = total % 60;
    CString text;
    if (h > 0)
        text.Format(_T("%lld:%02lld:%02lld"), h, m, s);
    else
        text.Format(_T("%lld:%02lld"), m, s);
    return text;
}

CString FormatHoursMinutesShort(int64_t ms) {
    const int64_t minutes = (ms < 0 ? 0 : ms) / 60000;
    CString text;
    text.Format(_T("%lld:%02lld"), minutes / 60, minutes % 60);
    return text;
}

CString FormatHoursMinutesLong(int64_t ms) {
    const int64_t minutes = (ms < 0 ? 0 : ms) / 60000;
    CString text;
    if (minutes >= 60)
        text.Format(_T("%lld시간 %lld분"), minutes / 60, minutes % 60);
    else
        text.Format(_T("%lld분"), minutes);
    return text;
}

CString GetDataDir() {
    static CString dir;
    if (!dir.IsEmpty()) return dir;

#ifdef _DEBUG
    // 개발 중에는 실제 ProgramData 를 건드리지 않도록 경로를 바꿀 수 있다 (Release 빌드에는 없음).
    TCHAR overrideDir[MAX_PATH] = {};
    if (GetEnvironmentVariable(_T("CHILDRENBLOCK_DATADIR"), overrideDir, MAX_PATH) > 0) {
        dir = overrideDir;
        CreateDirectory(dir, nullptr);
        return dir;
    }
#endif

    PWSTR programData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData))) {
        dir = programData;
        CoTaskMemFree(programData);
    } else {
        dir = _T("C:\\ProgramData");
    }
    dir += _T("\\ChildrenBlock");
    CreateDirectory(dir, nullptr);
    return dir;
}

void LogEvent(LPCTSTR format, ...) {
    CString message;
    va_list args;
    va_start(args, format);
    message.FormatV(format, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    CString line;
    line.Format(_T("%04u-%02u-%02u %02u:%02u:%02u  %s\n"), st.wYear, st.wMonth, st.wDay, st.wHour,
                st.wMinute, st.wSecond, message.GetString());

    FILE* file = nullptr;
    if (_wfopen_s(&file, GetDataDir() + _T("\\log.txt"), L"a, ccs=UTF-8") == 0 && file) {
        fputws(line, file);
        fclose(file);
    }
    TRACE(_T("%s"), line.GetString());
}

int ScaleForDpi(int value) {
    static int dpi = 0;
    if (dpi == 0) {
        HDC hdc = ::GetDC(nullptr);
        dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ::ReleaseDC(nullptr, hdc);
        if (dpi <= 0) dpi = 96;
    }
    return MulDiv(value, dpi, 96);
}
