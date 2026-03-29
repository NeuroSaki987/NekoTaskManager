#include "App.h"


namespace {
bool EnablePrivilege(PCWSTR privilegeName)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    LUID luid{};
    const BOOL lookedUp = LookupPrivilegeValueW(nullptr, privilegeName, &luid);
    if (lookedUp) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    const bool ok = lookedUp && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    EnablePrivilege(SE_DEBUG_NAME);

    App app;
    if (!app.Initialize(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"NekoTaskManager \u521D\u59CB\u5316\u5931\u8D25\u3002", L"NekoTaskManager", MB_ICONERROR);
        return -1;
    }
    return app.Run();
}
