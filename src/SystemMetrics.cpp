#include "SystemMetrics.h"

namespace {
constexpr size_t kHistoryLimit = 90;
constexpr PDH_STATUS kPdhMoreData = static_cast<PDH_STATUS>(0x800007D2L);

struct NetReadResult {
    uint64_t totalIn = 0;
    uint64_t totalOut = 0;
    uint64_t ethernetIn = 0;
    uint64_t ethernetOut = 0;
    bool ethernetConnected = false;
};

std::wstring HexNoPad(unsigned long value)
{
    std::wstringstream ss;
    ss << std::hex << std::nouppercase << value;
    return ss.str();
}

NetReadResult ReadNetworkBytes()
{
    NetReadResult result{};
    ULONG size = 0;
    if (GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER) {
        return result;
    }

    std::vector<BYTE> buffer(size);
    auto* table = reinterpret_cast<MIB_IFTABLE*>(buffer.data());
    if (GetIfTable(table, &size, FALSE) != NO_ERROR) {
        return result;
    }

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW& row = table->table[i];
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const uint64_t inBytes = static_cast<uint64_t>(row.dwInOctets);
        const uint64_t outBytes = static_cast<uint64_t>(row.dwOutOctets);
        if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL) {
            result.totalIn += inBytes;
            result.totalOut += outBytes;
        }
        if (row.dwType == IF_TYPE_ETHERNET_CSMACD && row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL) {
            result.ethernetConnected = true;
            result.ethernetIn += inBytes;
            result.ethernetOut += outBytes;
        }
    }
    return result;
}

std::wstring ReadRegistryString(HKEY rootKey, const wchar_t* subkey, const wchar_t* valueName)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(rootKey, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return L"";
    DWORD type = 0;
    DWORD cb = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || cb < sizeof(wchar_t)) {
        RegCloseKey(key);
        return L"";
    }
    std::wstring value(cb / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &cb) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return L"";
    }
    RegCloseKey(key);
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}
}

SystemMetrics::SystemMetrics() = default;

SystemMetrics::~SystemMetrics()
{
    if (m_query) {
        PdhCloseQuery(m_query);
        m_query = nullptr;
    }
}

bool SystemMetrics::AddCounter(const wchar_t* path, PDH_HCOUNTER& counter)
{
    return PdhAddEnglishCounterW(m_query, path, 0, &counter) == ERROR_SUCCESS;
}

double SystemMetrics::ReadCounter(PDH_HCOUNTER counter) const
{
    if (!counter) return 0.0;
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS || value.CStatus != ERROR_SUCCESS) {
        return 0.0;
    }
    return value.doubleValue;
}

void SystemMetrics::PushHistory(std::deque<double>& history, double value)
{
    history.push_back(std::clamp(value, 0.0, 100.0));
    while (history.size() > kHistoryLimit) history.pop_front();
}

void SystemMetrics::RefreshStaticSystemInfo()
{
    m_snapshot.cpuName = ReadRegistryString(HKEY_LOCAL_MACHINE, LR"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)", L"ProcessorNameString");
    m_snapshot.socketCount = 0;
    m_snapshot.groupCount = 0;
    m_snapshot.numaNodeCount = 0;
    m_snapshot.physicalCoreCount = 0;
    m_snapshot.logicalCoreCount = 0;
    m_snapshot.performanceCoreCount = 0;
    m_snapshot.efficiencyCoreCount = 0;

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    if (len == 0) return;
    std::vector<BYTE> buffer(len);
    if (!GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &len)) return;

    uint32_t minEff = 255;
    uint32_t maxEff = 0;
    std::vector<uint32_t> efficiencyClasses;

    BYTE* ptr = buffer.data();
    BYTE* end = buffer.data() + len;
    while (ptr < end) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
        switch (info->Relationship) {
            case RelationProcessorPackage:
                ++m_snapshot.socketCount;
                break;
            case RelationGroup:
                m_snapshot.groupCount = info->Group.ActiveGroupCount;
                break;
            case RelationNumaNode:
                ++m_snapshot.numaNodeCount;
                break;
            case RelationProcessorCore: {
                ++m_snapshot.physicalCoreCount;
                const auto& rel = info->Processor;
                efficiencyClasses.push_back(rel.EfficiencyClass);
                minEff = (std::min)(minEff, static_cast<uint32_t>(rel.EfficiencyClass));
                maxEff = (std::max)(maxEff, static_cast<uint32_t>(rel.EfficiencyClass));
                for (WORD g = 0; g < rel.GroupCount; ++g) {
                    unsigned long long maskBits = static_cast<unsigned long long>(rel.GroupMask[g].Mask);
                    uint32_t bitCount = 0;
                    while (maskBits) { bitCount += static_cast<uint32_t>(maskBits & 1ull); maskBits >>= 1ull; }
                    m_snapshot.logicalCoreCount += bitCount;
                }
                break;
            }
        }
        ptr += info->Size;
    }

    if (!efficiencyClasses.empty()) {
        if (minEff == maxEff) {
            m_snapshot.performanceCoreCount = m_snapshot.physicalCoreCount;
        } else {
            for (uint32_t eff : efficiencyClasses) {
                if (eff == minEff) ++m_snapshot.performanceCoreCount;
                else ++m_snapshot.efficiencyCoreCount;
            }
        }
    }

    if (m_snapshot.groupCount == 0) m_snapshot.groupCount = 1;
}

void SystemMetrics::RefreshCpuSetInfo()
{
    ULONG needed = 0;
    GetSystemCpuSetInformation(nullptr, 0, &needed, nullptr, 0);
    if (needed == 0) {
        m_snapshot.parkedLogicalCount = 0;
        return;
    }
    std::vector<BYTE> buffer(needed);
    if (!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), needed, &needed, nullptr, 0)) {
        m_snapshot.parkedLogicalCount = 0;
        return;
    }
    uint32_t parked = 0;
    BYTE* ptr = buffer.data();
    BYTE* end = buffer.data() + needed;
    while (ptr < end) {
        auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(ptr);
        if (info->Type == CpuSetInformation && info->CpuSet.Parked) ++parked;
        ptr += info->Size;
    }
    m_snapshot.parkedLogicalCount = parked;
}

bool SystemMetrics::InitializeGpuSelection()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        m_selectedGpuName = L"GPU";
        return false;
    }

    SIZE_T bestScore = 0;
    bool found = false;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            const bool discrete = desc.DedicatedVideoMemory > 0;
            const SIZE_T score = discrete ? desc.DedicatedVideoMemory : ((desc.SharedSystemMemory > 0) ? desc.SharedSystemMemory : 1);
            if (!found || (discrete && !m_selectedGpuIsDiscrete) || score > bestScore) {
                bestScore = score;
                found = true;
                m_selectedGpuIsDiscrete = discrete;
                m_selectedGpuName = desc.Description;
                m_selectedGpuLuidTag = L"luid_0x" + HexNoPad(desc.AdapterLuid.HighPart) + L"_0x" + HexNoPad(desc.AdapterLuid.LowPart);
            }
        }
        adapter->Release();
    }
    factory->Release();

    if (!found) {
        m_selectedGpuName = L"GPU";
        m_selectedGpuLuidTag.clear();
    }
    m_snapshot.gpuName = m_selectedGpuName;
    m_snapshot.gpuIsDiscrete = m_selectedGpuIsDiscrete;
    return found;
}

double SystemMetrics::ReadGpuTotal()
{
    if (!m_gpuWildcardCounter) return 0.0;

    DWORD bufSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(m_gpuWildcardCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
    if (status != kPdhMoreData || bufSize == 0 || itemCount == 0) return 0.0;

    std::vector<BYTE> buffer(bufSize);
    auto* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
    status = PdhGetFormattedCounterArrayW(m_gpuWildcardCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, items);
    if (status != ERROR_SUCCESS) return 0.0;

    double total = 0.0;
    double fallbackTotal = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        const std::wstring name = items[i].szName ? items[i].szName : L"";
        const auto& fv = items[i].FmtValue;
        if (fv.CStatus != ERROR_SUCCESS || !std::isfinite(fv.doubleValue) || fv.doubleValue < 0.0) continue;
        if (name.find(L"engtype_3D") != std::wstring::npos || name.find(L"engtype_Compute") != std::wstring::npos || name.find(L"engtype_Cuda") != std::wstring::npos) {
            fallbackTotal += fv.doubleValue;
        }
        if (!m_selectedGpuLuidTag.empty() && name.find(m_selectedGpuLuidTag) != std::wstring::npos) {
            total += fv.doubleValue;
        }
    }
    if (total <= 0.0) total = fallbackTotal;
    return std::clamp(total, 0.0, 100.0);
}

bool SystemMetrics::Initialize()
{
    if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) return false;
    AddCounter(LR"(\Processor(_Total)\% Processor Time)", m_cpuCounter);
    AddCounter(LR"(\Processor Information(_Total)\% Processor Utility)", m_cpuUtilityCounter);
    AddCounter(LR"(\PhysicalDisk(_Total)\% Disk Time)", m_diskCounter);
    AddCounter(LR"(\GPU Engine(*)\Utilization Percentage)", m_gpuWildcardCounter);
    InitializeGpuSelection();
    RefreshStaticSystemInfo();
    RefreshCpuSetInfo();
    PdhCollectQueryData(m_query);
    return true;
}

void SystemMetrics::Sample()
{
    if (m_query) PdhCollectQueryData(m_query);

    m_snapshot.cpuPercent = std::clamp(ReadCounter(m_cpuCounter), 0.0, 100.0);
    m_snapshot.cpuUtilityPercent = std::clamp(ReadCounter(m_cpuUtilityCounter), 0.0, 100.0);
    m_snapshot.diskPercent = std::clamp(ReadCounter(m_diskCounter), 0.0, 100.0);
    m_snapshot.gpuPercent = std::clamp(ReadGpuTotal(), 0.0, 100.0);
    m_snapshot.gpuName = m_selectedGpuName.empty() ? L"GPU" : m_selectedGpuName;
    m_snapshot.gpuIsDiscrete = m_selectedGpuIsDiscrete;

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        m_snapshot.memoryPercent = static_cast<double>(mem.dwMemoryLoad);
        m_snapshot.memoryTotal = mem.ullTotalPhys;
        m_snapshot.memoryUsed = mem.ullTotalPhys - mem.ullAvailPhys;
    }

    static NetReadResult prevNet{};
    static ULONGLONG prevTick = 0;
    const NetReadResult currentNet = ReadNetworkBytes();
    const ULONGLONG currentTick = GetTickCount64();
    m_snapshot.ethernetConnected = currentNet.ethernetConnected;
    m_snapshot.uptimeSeconds = currentTick / 1000ull;

    if (prevTick != 0 && currentTick > prevTick) {
        const double seconds = static_cast<double>(currentTick - prevTick) / 1000.0;
        if (seconds > 0.0) {
            m_snapshot.netInBytesPerSec = static_cast<uint64_t>(std::max(0.0, (static_cast<double>(currentNet.totalIn - prevNet.totalIn) / seconds)));
            m_snapshot.netOutBytesPerSec = static_cast<uint64_t>(std::max(0.0, (static_cast<double>(currentNet.totalOut - prevNet.totalOut) / seconds)));
            m_snapshot.ethernetInBytesPerSec = static_cast<uint64_t>(std::max(0.0, (static_cast<double>(currentNet.ethernetIn - prevNet.ethernetIn) / seconds)));
            m_snapshot.ethernetOutBytesPerSec = static_cast<uint64_t>(std::max(0.0, (static_cast<double>(currentNet.ethernetOut - prevNet.ethernetOut) / seconds)));
        }
    }
    prevNet = currentNet;
    prevTick = currentTick;

    PERFORMANCE_INFORMATION perf{};
    perf.cb = sizeof(perf);
    if (GetPerformanceInfo(&perf, sizeof(perf))) {
        m_snapshot.processCount = perf.ProcessCount;
        m_snapshot.threadCount = perf.ThreadCount;
    }

    PDH_HQUERY queueQuery = nullptr;
    PDH_HCOUNTER queueCounter = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &queueQuery) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(queueQuery, LR"(\System\Processor Queue Length)", 0, &queueCounter) == ERROR_SUCCESS) {
            PdhCollectQueryData(queueQuery);
            Sleep(1);
            PdhCollectQueryData(queueQuery);
            m_snapshot.cpuQueueLength = std::max(0.0, ReadCounter(queueCounter));
        } else {
            m_snapshot.cpuQueueLength = 0.0;
        }
        PdhCloseQuery(queueQuery);
    } else {
        m_snapshot.cpuQueueLength = 0.0;
    }

    m_snapshot.cpuQueueNormalized = (m_snapshot.logicalCoreCount > 0) ? (m_snapshot.cpuQueueLength / static_cast<double>(m_snapshot.logicalCoreCount)) : 0.0;
    const double pressurePenalty = std::min(100.0, m_snapshot.cpuQueueNormalized * 100.0 + std::max(0.0, m_snapshot.cpuUtilityPercent - 92.0) * 2.5);
    m_snapshot.responsivenessPercent = std::clamp(100.0 - pressurePenalty, 0.0, 100.0);

    RefreshCpuSetInfo();

    PushHistory(m_cpuHistory, m_snapshot.cpuUtilityPercent > 0.0 ? m_snapshot.cpuUtilityPercent : m_snapshot.cpuPercent);
    PushHistory(m_memHistory, m_snapshot.memoryPercent);
    PushHistory(m_gpuHistory, m_snapshot.gpuPercent);
    PushHistory(m_diskHistory, m_snapshot.diskPercent);
}
