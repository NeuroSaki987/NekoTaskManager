#include "ProcessManager.h"
#include <dbghelp.h>

namespace {
uint64_t FileTimeToUint64(const FILETIME& ft)
{
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

DWORD QueryFlags()
{
    return PROCESS_QUERY_LIMITED_INFORMATION;
}

DWORD MemoryFlags()
{
    return PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
}

typedef LONG (NTAPI* NtSuspendProcessFn)(HANDLE);
typedef LONG (NTAPI* NtResumeProcessFn)(HANDLE);

std::wstring TrimCopy(const std::wstring& value)
{
    const size_t start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    const size_t end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::wstring ExpandEnvCopy(const std::wstring& value)
{
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0) return value;
    std::wstring out(needed - 1, L'\0');
    ExpandEnvironmentStringsW(value.c_str(), out.data(), needed);
    return out;
}

std::wstring QuoteShellArg(const std::wstring& value)
{
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'\"') out += L'\\';
        out += ch;
    }
    out += L'\"';
    return out;
}

std::wstring JoinShellArgs(int argc, LPWSTR* argv, int startIndex)
{
    std::wstring joined;
    for (int i = startIndex; i < argc; ++i) {
        if (!joined.empty()) joined += L' ';
        joined += QuoteShellArg(argv[i]);
    }
    return joined;
}

bool ExecuteViaShell(const std::wstring& file, const std::wstring& parameters, bool runAsAdmin)
{
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = nullptr;
    sei.lpVerb = runAsAdmin ? L"runas" : L"open";
    sei.lpFile = file.c_str();
    sei.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

bool InvokeNtProcessOp(DWORD pid, bool suspend)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto fnSuspend = reinterpret_cast<NtSuspendProcessFn>(GetProcAddress(ntdll, "NtSuspendProcess"));
    auto fnResume = reinterpret_cast<NtResumeProcessFn>(GetProcAddress(ntdll, "NtResumeProcess"));
    auto fn = suspend ? reinterpret_cast<void*>(fnSuspend) : reinterpret_cast<void*>(fnResume);
    if (!fn) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return false;
    }

    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    const LONG status = suspend ? fnSuspend(h) : fnResume(h);
    CloseHandle(h);
    if (status < 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    return true;
}
}

namespace {
struct VisibleWindowProbe {
    DWORD pid = 0;
    bool found = false;
};

BOOL CALLBACK EnumVisibleWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* probe = reinterpret_cast<VisibleWindowProbe*>(lParam);
    if (!probe || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr || IsIconic(hwnd)) return TRUE;
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == probe->pid) {
        probe->found = true;
        return FALSE;
    }
    return TRUE;
}

bool HasVisibleTopLevelWindow(DWORD pid)
{
    VisibleWindowProbe probe{};
    probe.pid = pid;
    EnumWindows(EnumVisibleWindowsProc, reinterpret_cast<LPARAM>(&probe));
    return probe.found;
}

bool IsBackgroundName(const std::wstring& name)
{
    const std::wstring lower = ToLowerCopy(name);
    static const wchar_t* patterns[] = {
        L"service", L"host", L"broker", L"runtime", L"updater", L"helper", L"launcher",
        L"daemon", L"agent", L"svchost", L"taskhost", L"registry", L"searchindexer", L"wmi"
    };
    for (const auto* p : patterns) {
        if (lower.find(p) != std::wstring::npos) return true;
    }
    return false;
}
}

uint64_t ProcessManager::SystemTime100ns() const
{
    FILETIME idle{}, kernel{}, user{};
    GetSystemTimes(&idle, &kernel, &user);
    return FileTimeToUint64(kernel) + FileTimeToUint64(user);
}

void ProcessManager::Refresh()
{
    std::map<DWORD, ProcessInfo> oldByPid;
    for (const auto& p : m_processes) {
        oldByPid[p.pid] = p;
    }

    std::vector<ProcessInfo> updated;
    updated.reserve((std::max)(m_processes.size(), static_cast<size_t>(128)));

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    const uint64_t currentSystem = SystemTime100ns();
    const uint64_t systemDelta = m_lastSystemTick == 0 ? 0 : (currentSystem - m_lastSystemTick);
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const double cpuScale = (systemDelta > 0 && si.dwNumberOfProcessors > 0)
        ? (100.0 * static_cast<double>(si.dwNumberOfProcessors) / static_cast<double>(systemDelta))
        : 0.0;

    std::set<DWORD> alivePids;
    int expensivePathQueries = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessInfo p{};
            p.pid = entry.th32ProcessID;
            p.parentPid = entry.th32ParentProcessID;
            p.name = entry.szExeFile;
            p.threadCount = entry.cntThreads;
            p.isSuspended = m_suspendedPids.find(p.pid) != m_suspendedPids.end();
            alivePids.insert(p.pid);

            const auto oldIt = oldByPid.find(p.pid);
            if (oldIt != oldByPid.end()) {
                p.path = oldIt->second.path;
                p.workingSet = oldIt->second.workingSet;
                p.prevKernel = oldIt->second.prevKernel;
                p.prevUser = oldIt->second.prevUser;
                p.cpuPercent = oldIt->second.cpuPercent;
            }

            HANDLE hQuery = OpenProcess(QueryFlags(), FALSE, p.pid);
            if (hQuery) {
                if (p.path.empty() && expensivePathQueries < 10) {
                    std::vector<wchar_t> pathBuf(4096, L'\0');
                    DWORD size = static_cast<DWORD>(pathBuf.size());
                    if (QueryFullProcessImageNameW(hQuery, 0, pathBuf.data(), &size) && size > 0) {
                        p.path.assign(pathBuf.data(), size);
                    }
                    ++expensivePathQueries;
                }

                FILETIME create{}, exitt{}, kernel{}, user{};
                if (GetProcessTimes(hQuery, &create, &exitt, &kernel, &user)) {
                    const uint64_t k = FileTimeToUint64(kernel);
                    const uint64_t u = FileTimeToUint64(user);
                    if (oldIt != oldByPid.end() && cpuScale > 0.0) {
                        const uint64_t delta = (k - oldIt->second.prevKernel) + (u - oldIt->second.prevUser);
                        p.cpuPercent = std::clamp(static_cast<double>(delta) * cpuScale, 0.0, 100.0);
                    }
                    if (p.isSuspended) {
                        p.cpuPercent = 0.0;
                    }
                    p.prevKernel = k;
                    p.prevUser = u;
                }
                CloseHandle(hQuery);
            }

            HANDLE hMem = OpenProcess(MemoryFlags(), FALSE, p.pid);
            if (hMem) {
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                if (GetProcessMemoryInfo(hMem, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                    p.workingSet = pmc.WorkingSetSize;
                }
                CloseHandle(hMem);
            }

            p.hasVisibleWindow = HasVisibleTopLevelWindow(p.pid);
            const bool hasPath = !p.path.empty();
            const bool inWindowsDir = hasPath && ToLowerCopy(p.path).find(L"\\windows\\") != std::wstring::npos;
            p.isAppLike = p.hasVisibleWindow || (hasPath && !inWindowsDir && !IsBackgroundName(p.name));
            p.isForegroundLike = p.isAppLike;
            updated.push_back(std::move(p));
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    for (auto it = m_suspendedPids.begin(); it != m_suspendedPids.end(); ) {
        if (alivePids.find(*it) == alivePids.end()) it = m_suspendedPids.erase(it);
        else ++it;
    }
    std::sort(updated.begin(), updated.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        if (a.isAppLike != b.isAppLike) return a.isAppLike > b.isAppLike;
        if (_wcsicmp(a.name.c_str(), b.name.c_str()) != 0) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        }
        return a.pid < b.pid;
    });
    m_lastSystemTick = currentSystem;
    m_processes = std::move(updated);
}

bool ProcessManager::KillProcess(DWORD pid) const
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return false;
    const BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok == TRUE;
}

bool ProcessManager::KillProcessTree(DWORD pid) const
{
    std::multimap<DWORD, DWORD> children;
    for (const auto& p : m_processes) {
        children.emplace(p.parentPid, p.pid);
    }

    std::vector<DWORD> order;
    std::function<void(DWORD)> walk = [&](DWORD current) {
        auto range = children.equal_range(current);
        for (auto it = range.first; it != range.second; ++it) walk(it->second);
        order.push_back(current);
    };
    walk(pid);

    bool any = false;
    bool allOk = true;
    for (DWORD targetPid : order) {
        if (targetPid == 0 || targetPid == 4) continue;
        any = true;
        if (!KillProcess(targetPid)) allOk = false;
    }
    return any && allOk;
}

bool ProcessManager::SetPriorityClassByPid(DWORD pid, DWORD priorityClass) const
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return false;
    const BOOL ok = SetPriorityClass(h, priorityClass);
    CloseHandle(h);
    return ok == TRUE;
}

bool ProcessManager::SetAffinityMaskByPid(DWORD pid, DWORD_PTR mask) const
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return false;
    const BOOL ok = SetProcessAffinityMask(h, mask);
    CloseHandle(h);
    return ok == TRUE;
}

bool ProcessManager::SuspendProcess(DWORD pid)
{
    if (pid == 0 || pid == 4) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    if (!InvokeNtProcessOp(pid, true)) return false;
    m_suspendedPids.insert(pid);
    return true;
}

bool ProcessManager::ResumeProcess(DWORD pid)
{
    if (pid == 0 || pid == 4) {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    if (!InvokeNtProcessOp(pid, false)) return false;
    m_suspendedPids.erase(pid);
    return true;
}

bool ProcessManager::LaunchProcess(const std::wstring& commandLine, bool runAsAdmin) const
{
    const std::wstring expanded = TrimCopy(ExpandEnvCopy(commandLine));
    if (expanded.empty()) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(expanded.c_str(), &argc);
    if (argv && argc > 0) {
        const std::wstring file = argv[0];
        const std::wstring params = JoinShellArgs(argc, argv, 1);
        const bool ok = ExecuteViaShell(file, params, runAsAdmin);
        LocalFree(argv);
        if (ok) return true;
    }

    return ExecuteViaShell(expanded, L"", runAsAdmin);
}

bool ProcessManager::LaunchProcessViaCmd(const std::wstring& commandLine) const
{
    std::wstring cmd = L"cmd.exe /C " + commandLine;
    return LaunchProcess(cmd, false);
}

bool ProcessManager::CreateDumpFile(DWORD pid, const std::wstring& outPath) const
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, pid);
    if (!hProcess) return false;
    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        CloseHandle(hProcess);
        return false;
    }
    const BOOL ok = MiniDumpWriteDump(hProcess, pid, hFile, MiniDumpWithFullMemory, nullptr, nullptr, nullptr);
    CloseHandle(hFile);
    CloseHandle(hProcess);
    return ok == TRUE;
}

std::vector<FlatProcessNode> ProcessManager::BuildTree(const std::wstring& filter, const std::set<DWORD>& expanded) const
{
    (void)expanded;
    std::vector<FlatProcessNode> flat;
    flat.reserve(m_processes.size());
    const std::wstring filterLower = ToLowerCopy(filter);

    for (const auto& p : m_processes) {
        if (!filterLower.empty()) {
            const std::wstring name = ToLowerCopy(p.name);
            const std::wstring path = ToLowerCopy(p.path);
            const std::wstring pid = std::to_wstring(p.pid);
            if (name.find(filterLower) == std::wstring::npos &&
                path.find(filterLower) == std::wstring::npos &&
                pid.find(filterLower) == std::wstring::npos) {
                continue;
            }
        }
        flat.push_back(FlatProcessNode{ &p, 0, false, false });
    }
    return flat;
}
