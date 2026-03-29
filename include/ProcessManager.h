#pragma once
#include "Common.h"

struct ProcessInfo {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring name;
    std::wstring path;
    SIZE_T workingSet = 0;
    DWORD threadCount = 0;
    double cpuPercent = 0.0;
    uint64_t prevKernel = 0;
    uint64_t prevUser = 0;
    bool hasVisibleWindow = false;
    bool isForegroundLike = false;
    bool isAppLike = false;
    bool isSuspended = false;
};

struct FlatProcessNode {
    const ProcessInfo* process = nullptr;
    int depth = 0;
    bool hasChildren = false;
    bool expanded = false;
};

class ProcessManager {
public:
    void Refresh();
    const std::vector<ProcessInfo>& Processes() const { return m_processes; }
    std::vector<FlatProcessNode> BuildTree(const std::wstring& filter, const std::set<DWORD>& expanded) const;

    bool KillProcess(DWORD pid) const;
    bool KillProcessTree(DWORD pid) const;
    bool SetPriorityClassByPid(DWORD pid, DWORD priorityClass) const;
    bool SetAffinityMaskByPid(DWORD pid, DWORD_PTR mask) const;
    bool SuspendProcess(DWORD pid);
    bool ResumeProcess(DWORD pid);
    bool LaunchProcess(const std::wstring& commandLine, bool runAsAdmin = false) const;
    bool LaunchProcessViaCmd(const std::wstring& commandLine) const;
    bool CreateDumpFile(DWORD pid, const std::wstring& outPath) const;

private:
    uint64_t SystemTime100ns() const;
    std::vector<ProcessInfo> m_processes;
    std::set<DWORD> m_suspendedPids;
    uint64_t m_lastSystemTick = 0;
};
