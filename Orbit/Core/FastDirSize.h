#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Orbit::Core
{

struct DirVisit
{
    uint64_t fileBytes{ 0 };
    uint32_t fileCount{ 0 };
    std::vector<std::wstring> subdirs;
};

// Fast directory walk: GetFileInformationByHandleEx(FileFullDirectoryInfo) with a
// 64 KiB buffer (TreeSize/WinDirStat-class syscall batching). Falls back to
// FindFirstFileExW + FIND_FIRST_EX_LARGE_FETCH.
class FastDirSize
{
public:
    static DirVisit Visit(
        std::wstring const& dir,
        std::atomic<bool> const& cancel,
        std::wstring const& startRoot = {},
        std::wstring_view excludeImmediate = {});

    static uint64_t Measure(
        std::wstring_view root,
        std::atomic<bool> const& cancel,
        std::atomic<uint32_t>* filesFound,
        std::atomic<uint64_t>* bytesFound,
        unsigned workers = 1,
        std::wstring_view excludeImmediate = {},
        std::atomic<uint64_t>* liveSize = nullptr);
};

} // namespace Orbit::Core
