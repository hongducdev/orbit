#include "pch.h"
#include "DiskAnalyzer.h"
#include "FastDirSize.h"
#include "../Platform/PathHelpers.h"

#include <winioctl.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

using Orbit::Platform::PathHelpers;

namespace
{
    bool ShouldSkipName(std::wstring const& name)
    {
        return name == L"." || name == L".." || PathHelpers::IsSystemVolumeInfo(name);
    }

    std::unique_ptr<Orbit::Core::FileNode> MakeNode(
        std::wstring name,
        std::wstring path,
        bool isDir,
        FILETIME lastWrite)
    {
        auto node = std::make_unique<Orbit::Core::FileNode>();
        node->name = std::move(name);
        node->path = std::move(path);
        node->isDir = isDir;
        node->lastWrite = lastWrite;
        return node;
    }

    bool DriveHasSeekPenalty(std::wstring const& path) noexcept
    {
        if (path.size() < 2 || path[1] != L':') return false;
        wchar_t volume[] = { L'\\', L'\\', L'.', L'\\', path[0], L':', 0 };
        HANDLE handle = ::CreateFileW(
            volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR desc{};
        DWORD bytes = 0;
        BOOL ok = ::DeviceIoControl(
            handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            &desc,
            sizeof(desc),
            &bytes,
            nullptr);
        ::CloseHandle(handle);
        return ok && desc.IncursSeekPenalty;
    }

    unsigned WorkerCount(unsigned requested, unsigned ownerCount, bool rotational) noexcept
    {
        unsigned hardware = std::thread::hardware_concurrency();
        if (hardware == 0) hardware = 4;
        unsigned cap = rotational ? 4u : (std::min)(16u, hardware);
        unsigned n = requested ? requested : cap;
        if (ownerCount) n = (std::min)(n, ownerCount);
        return (std::max)(1u, n);
    }
}

namespace Orbit::Core
{
    uint64_t DiskAnalyzer::RollupSizes(FileNode& node) noexcept
    {
        if (!node.isDir || node.children.empty()) return node.size;
        uint64_t sum = 0;
        uint64_t logical = 0;
        for (auto& child : node.children)
        {
            sum += RollupSizes(*child);
            logical += child->logicalSize;
        }
        node.size = sum;
        node.logicalSize = logical;
        return node.size;
    }

    std::unique_ptr<FileNode> DiskAnalyzer::ListImmediate(
        std::wstring_view root,
        AnalyzeOptions const& options,
        std::atomic<bool> const& cancel)
    {
        auto rootNode = std::make_unique<FileNode>();
        rootNode->path = std::wstring(root);
        while (!rootNode->path.empty() &&
            (rootNode->path.back() == L'\\' || rootNode->path.back() == L'/') &&
            rootNode->path.size() > 3)
        {
            rootNode->path.pop_back();
        }
        rootNode->name = rootNode->path;
        rootNode->isDir = true;

        std::wstring pattern = PathHelpers::Join(rootNode->path, L"*");
        WIN32_FIND_DATAW data{};
        HANDLE find = ::FindFirstFileExW(
            PathHelpers::EnsureLongPath(pattern).c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE)
        {
            rootNode->childrenComplete = true;
            return rootNode;
        }

        size_t count = 1;
        do
        {
            if (cancel.load() || count >= options.maxNodes) break;
            std::wstring name = data.cFileName;
            if (ShouldSkipName(name)) continue;

            std::wstring full = PathHelpers::Join(rootNode->path, name);
            bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool isCloud = PathHelpers::IsOneDrivePlaceholder(data.dwFileAttributes);
            if (PathHelpers::IsReparseJunction(data.dwFileAttributes) && !isCloud &&
                !options.followReparse)
            {
                continue;
            }

            auto child = MakeNode(std::move(name), full, isDir, data.ftLastWriteTime);
            child->isCloudPlaceholder = isCloud;
            uint64_t logical =
                (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            child->logicalSize = logical;
            if (!isDir || isCloud)
            {
                child->size = isCloud ? 0 : logical;
                child->childrenComplete = true;
            }
            child->hint = InferFileCategoryHint(child->path, child->size ? child->size : logical);
            rootNode->children.push_back(std::move(child));
            ++count;
        } while (::FindNextFileW(find, &data));

        ::FindClose(find);
        rootNode->childrenComplete = true;
        RollupSizes(*rootNode);
        return rootNode;
    }

    void DiskAnalyzer::SizeChildrenParallel(
        FileNode& root,
        std::atomic<bool> const& cancel,
        std::atomic<uint32_t>& filesFound,
        std::atomic<uint64_t>& bytesFound,
        unsigned workers,
        std::mutex* treeMutex,
        std::atomic<uint32_t>* dirsDone)
    {
        std::vector<FileNode*> owners;
        owners.reserve(root.children.size());
        for (auto& child : root.children)
        {
            if (child->isDir && !child->isCloudPlaceholder) owners.push_back(child.get());
        }
        if (owners.empty()) return;

        bool rotational = DriveHasSeekPenalty(root.path);
        auto sizes = std::make_unique<std::atomic<uint64_t>[]>(owners.size());
        for (uint32_t i = 0; i < owners.size(); ++i) sizes[i].store(0, std::memory_order_relaxed);
        std::atomic<size_t> next{ 0 };
        std::atomic<uint32_t> finished{ 0 };

        auto flushLive = [&] {
            std::unique_lock<std::mutex> lock;
            if (treeMutex) lock = std::unique_lock<std::mutex>(*treeMutex);
            uint64_t sum = 0;
            for (uint32_t i = 0; i < owners.size(); ++i)
            {
                uint64_t live = sizes[i].load(std::memory_order_relaxed);
                if (live > owners[i]->size)
                {
                    owners[i]->size = live;
                    owners[i]->logicalSize = live;
                }
            }
            for (auto& child : root.children) sum += child->size;
            root.size = sum;
        };

        auto worker = [&] {
            while (!cancel.load(std::memory_order_relaxed))
            {
                size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= owners.size()) return;
                uint32_t owner = static_cast<uint32_t>(index);
                uint64_t size = FastDirSize::Measure(
                    owners[owner]->path,
                    cancel,
                    &filesFound,
                    &bytesFound,
                    1,
                    {},
                    &sizes[owner]);
                sizes[owner].store(size, std::memory_order_relaxed);
                if (dirsDone) dirsDone->fetch_add(1, std::memory_order_relaxed);
                finished.fetch_add(1, std::memory_order_relaxed);
            }
        };

        unsigned n = WorkerCount(workers, static_cast<unsigned>(owners.size()), rotational);
        std::vector<std::thread> pool;
        pool.reserve(n);
        for (unsigned i = 0; i < n; ++i) pool.emplace_back(worker);
        while (finished.load(std::memory_order_relaxed) < owners.size() &&
            !cancel.load(std::memory_order_relaxed))
        {
            flushLive();
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
        for (auto& thread : pool) thread.join();

        std::unique_lock<std::mutex> lock;
        if (treeMutex) lock = std::unique_lock<std::mutex>(*treeMutex);
        for (uint32_t i = 0; i < owners.size(); ++i)
        {
            owners[i]->size = sizes[i].load(std::memory_order_relaxed);
            owners[i]->logicalSize = owners[i]->size;
            owners[i]->hint = InferFileCategoryHint(owners[i]->path, owners[i]->size);
            owners[i]->childrenComplete = false;
        }
        RollupSizes(root);
    }

    std::unique_ptr<FileNode> DiskAnalyzer::Analyze(
        std::wstring_view root,
        AnalyzeOptions const& options,
        HardlinkTracker&,
        std::atomic<bool> const& cancel,
        ProgressCallback progress)
    {
        auto tree = ListImmediate(root, options, cancel);
        AnalyzeProgress state{};
        if (progress) progress(state);
        std::atomic<uint32_t> files{ 0 };
        std::atomic<uint64_t> bytes{ 0 };
        SizeChildrenParallel(*tree, cancel, files, bytes, options.sizeWorkers);
        state.filesFound = files.load();
        state.bytesFound = bytes.load();
        if (progress) progress(state);
        return tree;
    }
}
