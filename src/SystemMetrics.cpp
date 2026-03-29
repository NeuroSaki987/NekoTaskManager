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
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        const uint64_t inBytes = static_cast<uint64_t>(row.dwInOctets);
        const uint64_t outBytes = static_cast<uint64_t>(row.dwOutOctets);
        if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL) {
            result.totalIn += inBytes;
            result.totalOut += outBytes;
        }
        const bool isEthernet = row.dwType == IF_TYPE_ETHERNET_CSMACD;
        if (isEthernet) {
            if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL) {
                result.ethernetConnected = true;
                result.ethernetIn += inBytes;
                result.ethernetOut += outBytes;
            }
        }
    }
    return result;
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
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS) {
        return 0.0;
    }
    return value.doubleValue;
}

void SystemMetrics::PushHistory(std::deque<double>& history, double value)
{
    history.push_back(std::clamp(value, 0.0, 100.0));
    while (history.size() > kHistoryLimit) {
        history.pop_front();
    }
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
    if (m_selectedGpuLuidTag.empty()) return 0.0;

    DWORD counterListSize = 0;
    DWORD instanceListSize = 0;
    PDH_STATUS status = PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine", nullptr, &counterListSize, nullptr, &instanceListSize, PERF_DETAIL_WIZARD, 0);
    if (status != kPdhMoreData || instanceListSize == 0) {
        return 0.0;
    }

    std::vector<wchar_t> counterBuf(counterListSize ? counterListSize : 1, L'\0');
    std::vector<wchar_t> instanceBuf(instanceListSize, L'\0');
    status = PdhEnumObjectItemsW(nullptr, nullptr, L"GPU Engine", counterBuf.data(), &counterListSize, instanceBuf.data(), &instanceListSize, PERF_DETAIL_WIZARD, 0);
    if (status != ERROR_SUCCESS) {
        return 0.0;
    }

    double total = 0.0;
    for (const wchar_t* ptr = instanceBuf.data(); ptr && *ptr; ptr += wcslen(ptr) + 1) {
        std::wstring instance = ptr;
        if (instance.find(m_selectedGpuLuidTag) == std::wstring::npos) continue;
        std::wstring path = L"\\GPU Engine(" + instance + L")\\Utilization Percentage";
        PDH_HCOUNTER counter = nullptr;
        if (PdhAddEnglishCounterW(m_query, path.c_str(), 0, &counter) == ERROR_SUCCESS && counter) {
            PDH_FMT_COUNTERVALUE value{};
            if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
                total += value.doubleValue;
            }
            PdhRemoveCounter(counter);
        }
    }
    return std::clamp(total, 0.0, 100.0);
}

bool SystemMetrics::Initialize()
{
    if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) return false;
    AddCounter(L"\\Processor(_Total)\\% Processor Time", m_cpuCounter);
    AddCounter(L"\\PhysicalDisk(_Total)\\% Disk Time", m_diskCounter);
    InitializeGpuSelection();
    PdhCollectQueryData(m_query);
    return true;
}

void SystemMetrics::Sample()
{
    if (m_query) {
        PdhCollectQueryData(m_query);
    }

    m_snapshot.cpuPercent = std::clamp(ReadCounter(m_cpuCounter), 0.0, 100.0);
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

    PushHistory(m_cpuHistory, m_snapshot.cpuPercent);
    PushHistory(m_memHistory, m_snapshot.memoryPercent);
    PushHistory(m_gpuHistory, m_snapshot.gpuPercent);
    PushHistory(m_diskHistory, m_snapshot.diskPercent);
}
