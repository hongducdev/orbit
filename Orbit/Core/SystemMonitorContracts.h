#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <optional>

namespace Orbit::Core
{

enum class MetricId : uint8_t
{
    CpuTotal = 0,
    CpuPerCore, // expanded per-core; ring holds total only for v1
    Memory,     // % used or Available MBytes
    GpuEngine,  // % utilization (optional, N/A if no counter)
    DiskRead,   // bytes/sec
    DiskWrite,
    Network,    // bytes/sec total
    Battery,    // % remaining (or 255 if no battery)
    Uptime,     // seconds
    Count
};

constexpr size_t kMetricCount = static_cast<size_t>(MetricId::Count);
constexpr size_t kRingSize = 60; // 60s sparkline

struct RingBuffer60
{
    std::array<double, kRingSize> values{};
    size_t head{0};
    size_t count{0}; // filled slots up to 60

    void Push(double v) noexcept
    {
        values[head] = v;
        head = (head + 1) % kRingSize;
        if (count < kRingSize) ++count;
    }

    double Latest() const noexcept
    {
        if (count == 0) return 0.0;
        size_t idx = (head + kRingSize - 1) % kRingSize;
        return values[idx];
    }
};

struct MonitorSnapshot
{
    std::array<RingBuffer60, kMetricCount> rings{};
    // Latest convenience
    double cpuTotal{0.0};
    double memoryUsedPercent{0.0};
    uint64_t memoryAvailableBytes{0};
    double gpuUtil{0.0};
    bool gpuAvailable{false};
    uint64_t diskReadBps{0};
    uint64_t diskWriteBps{0};
    uint64_t netBps{0};
    uint8_t batteryPercent{255}; // 255 = no battery / unknown
    bool batteryCharging{false};
    uint64_t uptimeSeconds{0};
};

// HealthScore inputs (formula deferred to Phase 7; contract defines weighting inputs)
struct HealthScoreInputs
{
    double cpuTotal{0.0};
    double memoryUsedPercent{0.0};
    double diskQueueHint{0.0}; // derived from disk busy if available
    bool hasFailingCounter{false};
};

inline int HealthScore(const HealthScoreInputs& in) noexcept
{
    // Simple 0-100: penalize high CPU/mem; placeholder formula for Phase 1
    int score = 100;
    if (in.cpuTotal > 85.0) score -= 20;
    else if (in.cpuTotal > 70.0) score -= 10;
    if (in.memoryUsedPercent > 90.0) score -= 25;
    else if (in.memoryUsedPercent > 80.0) score -= 10;
    if (in.hasFailingCounter) score -= 5;
    if (score < 0) score = 0;
    return score;
}

// IMonitorSource — PDH primary, fallback chain documented in ADR-08.
struct IMonitorSource
{
    virtual ~IMonitorSource() = default;
    virtual bool Initialize() = 0; // open PDH query, add counters; return true if at least one counter added
    virtual std::optional<MonitorSnapshot> Poll() = 0; // collect one tick; returns nullopt on fatal failure
    virtual void Shutdown() = 0;
};

// Counter name constants (English). Use PdhAddEnglishCounterW to avoid localization issues.
namespace PdhCounterNames
{
    inline constexpr const wchar_t* kCpuTotal = L"\\Processor(_Total)\\% Processor Time";
    inline constexpr const wchar_t* kMemoryAvailableMB = L"\\Memory\\Available MBytes";
    inline constexpr const wchar_t* kDiskReadBps = L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec";
    inline constexpr const wchar_t* kDiskWriteBps = L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec";
    inline constexpr const wchar_t* kNetBytesTotal = L"\\Network Interface(*)\\Bytes Total/sec";
    inline constexpr const wchar_t* kGpuEngine = L"\\GPU Engine(*)\\Utilization Percentage";
}

} // namespace Orbit::Core
