#pragma once
#include "Common.h"

struct SystemSnapshot {
    double cpuPercent = 0.0;
    double memoryPercent = 0.0;
    uint64_t memoryUsed = 0;
    uint64_t memoryTotal = 0;
    double diskPercent = 0.0;
    uint64_t netInBytesPerSec = 0;
    uint64_t netOutBytesPerSec = 0;
    double gpuPercent = 0.0;
    std::wstring gpuName;
    bool gpuIsDiscrete = false;
    bool ethernetConnected = false;
    uint64_t ethernetInBytesPerSec = 0;
    uint64_t ethernetOutBytesPerSec = 0;
};

class SystemMetrics {
public:
    SystemMetrics();
    ~SystemMetrics();
    bool Initialize();
    void Sample();
    const SystemSnapshot& Snapshot() const { return m_snapshot; }
    const std::deque<double>& CpuHistory() const { return m_cpuHistory; }
    const std::deque<double>& MemHistory() const { return m_memHistory; }
    const std::deque<double>& GpuHistory() const { return m_gpuHistory; }
    const std::deque<double>& DiskHistory() const { return m_diskHistory; }

private:
    bool AddCounter(const wchar_t* path, PDH_HCOUNTER& counter);
    double ReadCounter(PDH_HCOUNTER counter) const;
    double ReadGpuTotal();
    bool InitializeGpuSelection();
    void PushHistory(std::deque<double>& history, double value);

    PDH_HQUERY m_query = nullptr;
    PDH_HCOUNTER m_cpuCounter = nullptr;
    PDH_HCOUNTER m_diskCounter = nullptr;
    PDH_HCOUNTER m_netRecvCounter = nullptr;
    PDH_HCOUNTER m_netSendCounter = nullptr;
    std::wstring m_selectedGpuName;
    std::wstring m_selectedGpuLuidTag;
    bool m_selectedGpuIsDiscrete = false;
    SystemSnapshot m_snapshot;
    std::deque<double> m_cpuHistory;
    std::deque<double> m_memHistory;
    std::deque<double> m_gpuHistory;
    std::deque<double> m_diskHistory;
};
