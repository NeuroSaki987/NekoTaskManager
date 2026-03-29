#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <imm.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <iomanip>
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <functional>
#include <filesystem>
#include <dbghelp.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "dbghelp.lib")

template<typename T>
inline void SafeRelease(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

template<typename T>
struct ComReleaser {
    void operator()(T* p) const { if (p) p->Release(); }
};

template<typename T>
using com_ptr = std::unique_ptr<T, ComReleaser<T>>;

inline std::wstring FormatBytes(uint64_t bytes)
{
    const wchar_t* suffixes[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double value = static_cast<double>(bytes);
    int idx = 0;
    while (value >= 1024.0 && idx < 4) {
        value /= 1024.0;
        ++idx;
    }
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(idx == 0 ? 0 : 1) << value << L' ' << suffixes[idx];
    return ss.str();
}

inline std::wstring ToLowerCopy(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

inline std::wstring FormatPercent1(double value)
{
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << value << L"%";
    return ss.str();
}
