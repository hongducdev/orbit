#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "FileNode.h"
#include "SizeCalculator.h"

namespace Orbit::Core
{

struct AnalyzeOptions
{
    int maxDepth{ 1 };
    size_t maxNodes{ 1'000'000 };
    bool followReparse{ false };
    unsigned sizeWorkers{ 0 };
};

struct AnalyzeProgress
{
    size_t filesFound{ 0 };
    uint64_t bytesFound{ 0 };
    std::wstring currentPath;
    bool truncated{ false };
};

class DiskAnalyzer
{
public:
    using ProgressCallback = std::function<void(const AnalyzeProgress&)>;

    static std::unique_ptr<FileNode> ListImmediate(
        std::wstring_view root,
        AnalyzeOptions const& options,
        std::atomic<bool> const& cancel);

    static void SizeChildrenParallel(
        FileNode& root,
        std::atomic<bool> const& cancel,
        std::atomic<uint32_t>& filesFound,
        std::atomic<uint64_t>& bytesFound,
        unsigned workers,
        std::mutex* treeMutex = nullptr,
        std::atomic<uint32_t>* dirsDone = nullptr);

    static std::unique_ptr<FileNode> Analyze(
        std::wstring_view root,
        AnalyzeOptions const& options,
        HardlinkTracker& tracker,
        std::atomic<bool> const& cancel,
        ProgressCallback progress);

    static uint64_t RollupSizes(FileNode& node) noexcept;
};

} // namespace Orbit::Core
