#include "pch.h"
#include "AnalyzeViewModel.h"

#include "../Core/FastDirSize.h"
#include "../Core/OperationLog.h"
#include "../Core/ProtectionList.h"
#include "../Platform/PathHelpers.h"

#include <algorithm>
#include <cwctype>
#include <thread>

namespace
{
    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), ::towlower);
        return value;
    }
}

namespace Orbit::ViewModels
{
    AnalyzeViewModel::AnalyzeViewModel() :
        m_whitelist(std::make_unique<Core::WhitelistStore>())
    {
    }

    std::vector<AnalyzeDrive> AnalyzeViewModel::EnumerateDrives(bool includeRemovable)
    {
        std::vector<AnalyzeDrive> drives;
        DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if ((mask & (1u << i)) == 0) continue;
            wchar_t root[] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', 0 };
            UINT type = ::GetDriveTypeW(root);
            if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) continue;
            bool removable = type == DRIVE_REMOVABLE || type == DRIVE_CDROM ||
                type == DRIVE_REMOTE || type == DRIVE_RAMDISK;
            if (removable && !includeRemovable) continue;

            AnalyzeDrive drive;
            drive.root = root;
            drive.type = type;
            wchar_t label[MAX_PATH]{};
            ::GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
            drive.label = label[0] ? label : (type == DRIVE_FIXED ? L"Local Disk" : L"Drive");
            ULARGE_INTEGER total{}, freeBytes{};
            if (::GetDiskFreeSpaceExW(root, nullptr, &total, &freeBytes))
            {
                drive.totalBytes = total.QuadPart;
                drive.freeBytes = freeBytes.QuadPart;
            }
            drives.push_back(std::move(drive));
        }
        return drives;
    }

    std::vector<AnalyzeListItem> AnalyzeViewModel::OverviewLocations(std::wstring const& driveRoot)
    {
        std::vector<AnalyzeListItem> items;
        AnalyzeListItem disk;
        disk.name = L"This disk";
        disk.path = driveRoot;
        disk.isDir = true;
        ULARGE_INTEGER totalBytes{}, freeBytes{};
        if (::GetDiskFreeSpaceExW(driveRoot.c_str(), nullptr, &totalBytes, &freeBytes) &&
            totalBytes.QuadPart >= freeBytes.QuadPart)
        {
            disk.size = totalBytes.QuadPart - freeBytes.QuadPart;
        }
        items.push_back(std::move(disk));

        std::wstring pattern = Platform::PathHelpers::Join(driveRoot, L"*");
        WIN32_FIND_DATAW data{};
        HANDLE find = ::FindFirstFileExW(
            Platform::PathHelpers::EnsureLongPath(pattern).c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) return items;

        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if (Platform::PathHelpers::IsSystemVolumeInfo(name)) continue;

            AnalyzeListItem item;
            item.name = name;
            item.path = Platform::PathHelpers::Join(driveRoot, name);
            item.isDir = true;
            items.push_back(std::move(item));
        } while (::FindNextFileW(find, &data));
        ::FindClose(find);

        std::sort(items.begin() + 1, items.end(), [](auto const& left, auto const& right) {
            return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
        });
        return items;
    }

    void AnalyzeViewModel::CancelOverview() noexcept
    {
        m_overviewCancel.store(true);
        m_overviewEpoch.fetch_add(1, std::memory_order_relaxed);
        isOverviewSizing.store(false);
        overviewStatus.clear();
    }

    void AnalyzeViewModel::ResetOverview(std::wstring const& driveRoot)
    {
        CancelOverview();
        auto items = OverviewLocations(driveRoot);
        std::lock_guard<std::mutex> lock(m_overviewMutex);
        m_overview = std::move(items);
    }

    std::vector<AnalyzeListItem> AnalyzeViewModel::OverviewSnapshot() const
    {
        std::lock_guard<std::mutex> lock(m_overviewMutex);
        return m_overview;
    }

    winrt::Windows::Foundation::IAsyncAction AnalyzeViewModel::RefreshOverviewAsync(
        std::wstring driveRoot)
    {
        uint32_t const epoch = m_overviewEpoch.fetch_add(1, std::memory_order_relaxed) + 1;
        m_overviewCancel.store(false);
        auto items = OverviewLocations(driveRoot);
        {
            std::lock_guard<std::mutex> lock(m_overviewMutex);
            m_overview = items;
        }
        isOverviewSizing.store(true);
        overviewStatus = L"Measuring…";
        winrt::apartment_context uiThread;

        co_await winrt::resume_background();
        std::atomic<size_t> next{ 0 };
        unsigned n = 1;
        std::vector<std::thread> pool;
        pool.reserve(n);
        for (unsigned i = 0; i < n; ++i)
        {
            pool.emplace_back([&] {
                while (!m_overviewCancel.load() &&
                    m_overviewEpoch.load(std::memory_order_relaxed) == epoch)
                {
                    size_t index = next.fetch_add(1);
                    if (index >= items.size()) break;
                    if (items[index].name == L"This disk") continue;
                    std::wstring exclude;
                    if (items[index].name == L"Home") exclude = L"AppData";
                    uint64_t size = Core::FastDirSize::Measure(
                        items[index].path,
                        m_overviewCancel,
                        nullptr,
                        nullptr,
                        1,
                        exclude);
                    if (m_overviewEpoch.load(std::memory_order_relaxed) != epoch) return;
                    std::lock_guard<std::mutex> lock(m_overviewMutex);
                    if (index < m_overview.size() &&
                        _wcsicmp(m_overview[index].path.c_str(), items[index].path.c_str()) == 0)
                    {
                        m_overview[index].size = size;
                    }
                }
            });
        }
        for (auto& thread : pool) thread.join();

        co_await uiThread;
        if (m_overviewEpoch.load(std::memory_order_relaxed) == epoch)
        {
            isOverviewSizing.store(false);
            overviewStatus.clear();
        }
    }

    winrt::Windows::Foundation::IAsyncAction AnalyzeViewModel::ScanAsync(std::wstring root)
    {
        bool expected = false;
        if (!isScanning.compare_exchange_strong(expected, true) || isDeleting.load())
        {
            co_return;
        }
        CancelOverview();
        cancelRequested.store(false);
        filesFound.store(0);
        bytesFound.store(0);
        dirsTotal.store(0);
        dirsDone.store(0);
        truncated = false;
        lastError.clear();
        scanStatus = L"Scanning " + root + L"…";
        previewReady.store(false);
        ClearSelection();
        winrt::apartment_context uiThread;

        co_await winrt::resume_background();
        Core::AnalyzeOptions options;
        options.maxDepth = 1;
        m_tracker.clear();
        std::unique_ptr<Core::FileNode> tree;
        try
        {
            tree = Core::DiskAnalyzer::ListImmediate(root, options, cancelRequested);
        }
        catch (...)
        {
            lastError = L"Scan failed";
        }

        co_await uiThread;
        {
            std::lock_guard<std::mutex> lock(m_treeMutex);
            m_root = std::move(tree);
            m_current = m_root.get();
            m_crumbs.clear();
            if (m_current) m_crumbs.push_back(m_current);
        }
        previewReady.store(m_root != nullptr);
        scanStatus = L"Mapping " + root + L"…";
        {
            uint32_t dirCount = 0;
            if (m_root)
            {
                for (auto const& child : m_root->children)
                {
                    if (child->isDir && !child->isCloudPlaceholder) ++dirCount;
                }
            }
            dirsTotal.store(dirCount);
        }

        co_await winrt::resume_background();
        try
        {
            if (m_root)
            {
                Core::DiskAnalyzer::SizeChildrenParallel(
                    *m_root,
                    cancelRequested,
                    filesFound,
                    bytesFound,
                    0,
                    &m_treeMutex,
                    &dirsDone);
            }
        }
        catch (...)
        {
            lastError = L"Scan failed";
        }

        co_await uiThread;
        scanStatus = !lastError.empty()
            ? lastError
            : (cancelRequested.load()
                ? L"Scan cancelled"
                : (truncated ? L"Scan truncated at 1M nodes" : L"Scan complete"));
        isScanning.store(false);
    }

    winrt::Windows::Foundation::IAsyncAction AnalyzeViewModel::ExpandCurrentAsync()
    {
        std::wstring targetPath;
        {
            std::lock_guard<std::mutex> lock(m_treeMutex);
            if (!m_current || !m_current->isDir || m_current->childrenComplete || isScanning.load())
            {
                co_return;
            }
            targetPath = m_current->path;
        }
        bool expected = false;
        if (!isScanning.compare_exchange_strong(expected, true)) co_return;
        cancelRequested.store(false);
        lastError.clear();
        filesFound.store(0);
        bytesFound.store(0);
        dirsTotal.store(0);
        dirsDone.store(0);
        scanStatus = L"Expanding " + targetPath + L"…";
        winrt::apartment_context uiThread;
        std::unique_ptr<Core::FileNode> expanded;
        try
        {
            co_await winrt::resume_background();
            Core::AnalyzeOptions options;
            expanded = Core::DiskAnalyzer::ListImmediate(
                targetPath, options, cancelRequested);
            if (expanded)
            {
                uint32_t dirCount = 0;
                for (auto const& child : expanded->children)
                {
                    if (child->isDir && !child->isCloudPlaceholder) ++dirCount;
                }
                dirsTotal.store(dirCount);
                Core::DiskAnalyzer::SizeChildrenParallel(
                    *expanded,
                    cancelRequested,
                    filesFound,
                    bytesFound,
                    0,
                    nullptr,
                    &dirsDone);
            }
        }
        catch (...)
        {
            lastError = L"Expand failed";
        }

        co_await uiThread;
        {
            std::lock_guard<std::mutex> lock(m_treeMutex);
            if (expanded && m_current &&
                _wcsicmp(m_current->path.c_str(), targetPath.c_str()) == 0)
            {
                m_current->children = std::move(expanded->children);
                m_current->childrenComplete = true;
                m_current->scanTruncated = expanded->scanTruncated;
                if (m_root) Core::DiskAnalyzer::RollupSizes(*m_root);
            }
        }
        scanStatus = lastError.empty() ? L"Folder expanded" : lastError;
        isScanning.store(false);
    }

    void AnalyzeViewModel::CancelScan() noexcept
    {
        cancelRequested.store(true);
    }

    Core::FileNode* AnalyzeViewModel::FindChild(std::wstring const& path) const noexcept
    {
        if (!m_current) return nullptr;
        for (auto const& child : m_current->children)
        {
            if (_wcsicmp(child->path.c_str(), path.c_str()) == 0) return child.get();
        }
        return nullptr;
    }

    bool AnalyzeViewModel::DrillIn(std::wstring const& path)
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        auto* child = FindChild(path);
        if (!child || !child->isDir) return false;
        m_current = child;
        m_crumbs.push_back(child);
        ClearSelection();
        return true;
    }

    bool AnalyzeViewModel::DrillUp()
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        if (m_crumbs.size() <= 1) return false;
        m_crumbs.pop_back();
        m_current = m_crumbs.back();
        ClearSelection();
        return true;
    }

    bool AnalyzeViewModel::NavigateToCrumb(size_t index)
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        if (index >= m_crumbs.size()) return false;
        m_crumbs.resize(index + 1);
        m_current = m_crumbs.back();
        ClearSelection();
        return true;
    }

    void AnalyzeViewModel::ToggleSelect(std::wstring const& path, bool additive)
    {
        if (!additive) m_selected.clear();
        if (m_selected.contains(path)) m_selected.erase(path);
        else m_selected.insert(path);
    }

    void AnalyzeViewModel::ClearSelection() noexcept
    {
        m_selected.clear();
    }

    void AnalyzeViewModel::SetFilter(std::wstring text)
    {
        m_filterLower = Lower(std::move(text));
    }

    Core::TreemapOptions AnalyzeViewModel::LayoutOptions() const
    {
        Core::TreemapOptions options;
        options.showFiles = showFiles;
        options.showFolders = showFolders;
        options.groupSmallBytes = isScanning.load() ? 0 : groupSmallBytes;
        options.sortByName = sortByName;
        options.nameFilterLower = m_filterLower;
        return options;
    }

    std::vector<Core::TreemapItem> AnalyzeViewModel::Layout(double width, double height) const
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        if (!m_current) return {};
        return Core::TreemapLayout::Layout(
            *m_current,
            { 0, 0, width, height },
            LayoutOptions());
    }

    std::vector<AnalyzeListItem> AnalyzeViewModel::VisibleListItems(bool stableOrder) const
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        std::vector<AnalyzeListItem> items;
        if (!m_current) return items;
        auto options = LayoutOptions();
        for (auto const& child : m_current->children)
        {
            if (child->isDir && !options.showFolders) continue;
            if (!child->isDir && !options.showFiles) continue;
            if (!options.nameFilterLower.empty())
            {
                if (Lower(child->name).find(options.nameFilterLower) == std::wstring::npos) continue;
            }
            AnalyzeListItem item;
            item.name = child->name;
            item.path = child->path;
            item.size = child->size;
            item.isDir = child->isDir;
            item.isCloud = child->isCloudPlaceholder;
            item.selected = m_selected.contains(child->path);
            item.hint = child->hint;
            item.lastWrite = child->lastWrite;
            items.push_back(std::move(item));
        }
        if (!stableOrder)
        {
            std::sort(items.begin(), items.end(), [&](auto const& left, auto const& right) {
                if (sortByName) return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
                return left.size > right.size;
            });
        }
        return items;
    }

    std::vector<std::wstring> AnalyzeViewModel::BreadcrumbLabels() const
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        std::vector<std::wstring> labels;
        for (auto* crumb : m_crumbs)
        {
            labels.push_back(crumb->name.empty() ? crumb->path : crumb->name);
        }
        return labels;
    }

    std::vector<std::wstring> AnalyzeViewModel::SelectedPaths() const
    {
        return { m_selected.begin(), m_selected.end() };
    }

    bool AnalyzeViewModel::NeedsExpand() const
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        return m_current && m_current->isDir && !m_current->childrenComplete;
    }

    std::wstring AnalyzeViewModel::StatusText() const
    {
        return scanStatus;
    }

    uint64_t AnalyzeViewModel::SelectedBytes() const noexcept
    {
        auto items = VisibleListItems();
        uint64_t bytes = 0;
        for (auto const& item : items)
        {
            if (item.selected) bytes += item.size;
        }
        return bytes;
    }

    uint32_t AnalyzeViewModel::SelectedCount() const noexcept
    {
        return static_cast<uint32_t>(m_selected.size());
    }

    bool AnalyzeViewModel::CanDeletePath(std::wstring const& path) const
    {
        if (Core::ProtectionList::IsSystemProtected(path)) return false;
        if (m_whitelist && m_whitelist->IsWhitelisted(path)) return false;
        std::lock_guard<std::mutex> lock(m_treeMutex);
        auto* child = FindChild(path);
        if (child && child->isCloudPlaceholder) return false;
        return true;
    }

    bool AnalyzeViewModel::AddWhitelist(std::wstring const& path)
    {
        return m_whitelist->Add(path, std::nullopt);
    }

    winrt::Windows::Foundation::IAsyncAction AnalyzeViewModel::DeleteSelectedAsync(bool permanent)
    {
        if (isDeleting.exchange(true)) co_return;
        try
        {
            auto paths = SelectedPaths();
            std::vector<std::wstring> allowed;
            uint64_t bytes = SelectedBytes();
            for (auto const& path : paths)
            {
                if (CanDeletePath(path)) allowed.push_back(path);
            }
            lastDeleteResult = {};
            lastOperationLogged = true;
            if (!allowed.empty())
            {
                lastDeleteResult = Platform::ShellOperations::DeleteFiles(allowed, permanent);
                Core::OperationLogEntry entry;
                entry.timestampIso8601 = Core::OperationLog::NowIso8601();
                entry.op = Core::OperationKind::Analyze;
                entry.category = L"analyze-treemap";
                entry.paths = lastDeleteResult.completedPaths;
                entry.bytesBefore = bytes;
                entry.dest = permanent ? Core::Destination::Permanent : Core::Destination::Recycle;
                entry.outcome = lastDeleteResult.succeeded
                    ? Core::OperationOutcome::Success
                    : (lastDeleteResult.IsPartial()
                        ? Core::OperationOutcome::Partial
                        : Core::OperationOutcome::Failed);
                entry.error = lastDeleteResult.error;
                lastOperationLogged = Core::OperationLog::Append(entry);
                RemoveDeletedNodes(lastDeleteResult.completedPaths);
            }
            ClearSelection();
        }
        catch (...)
        {
            lastDeleteResult.succeeded = false;
            lastDeleteResult.error = L"Delete failed";
        }
        isDeleting.store(false);
    }

    void AnalyzeViewModel::RemoveDeletedNodes(std::vector<std::wstring> const& paths)
    {
        std::lock_guard<std::mutex> lock(m_treeMutex);
        if (!m_current) return;
        auto& children = m_current->children;
        children.erase(
            std::remove_if(children.begin(), children.end(), [&](auto const& child) {
                return std::any_of(paths.begin(), paths.end(), [&](std::wstring const& path) {
                    return _wcsicmp(child->path.c_str(), path.c_str()) == 0;
                });
            }),
            children.end());
        if (m_root) Core::DiskAnalyzer::RollupSizes(*m_root);
    }
}
