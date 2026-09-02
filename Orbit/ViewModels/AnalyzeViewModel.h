#pragma once

#include <windows.h>
#include <winrt/Windows.Foundation.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "../Core/DiskAnalyzer.h"
#include "../Core/FileNode.h"
#include "../Core/TreemapLayout.h"
#include "../Core/WhitelistStore.h"
#include "../Platform/ShellOperations.h"

namespace Orbit::ViewModels
{

struct AnalyzeDrive
{
    std::wstring root;
    std::wstring label;
    UINT type{ DRIVE_UNKNOWN };
    uint64_t totalBytes{ 0 };
    uint64_t freeBytes{ 0 };
};

struct AnalyzeListItem
{
    std::wstring name;
    std::wstring path;
    uint64_t size{ 0 };
    bool isDir{ false };
    bool isCloud{ false };
    bool selected{ false };
    Core::FileCategoryHint hint{ Core::FileCategoryHint::Unknown };
    FILETIME lastWrite{};
};

class AnalyzeViewModel
{
public:
    AnalyzeViewModel();

    static std::vector<AnalyzeDrive> EnumerateDrives(bool includeRemovable);
    static std::vector<AnalyzeListItem> OverviewLocations(std::wstring const& driveRoot);

    winrt::Windows::Foundation::IAsyncAction RefreshOverviewAsync(std::wstring driveRoot);
    void CancelOverview() noexcept;
    void ResetOverview(std::wstring const& driveRoot);
    std::vector<AnalyzeListItem> OverviewSnapshot() const;

    winrt::Windows::Foundation::IAsyncAction ScanAsync(std::wstring root);
    winrt::Windows::Foundation::IAsyncAction ExpandCurrentAsync();
    void CancelScan() noexcept;

    bool DrillIn(std::wstring const& path);
    bool DrillUp();
    bool NavigateToCrumb(size_t index);
    void ToggleSelect(std::wstring const& path, bool additive);
    void ClearSelection() noexcept;
    void SetFilter(std::wstring text);

    std::vector<Core::TreemapItem> Layout(
        double width,
        double height) const;
    std::vector<AnalyzeListItem> VisibleListItems(bool stableOrder = false) const;
    std::vector<std::wstring> BreadcrumbLabels() const;
    std::vector<std::wstring> SelectedPaths() const;

    bool CanDeletePath(std::wstring const& path) const;
    bool AddWhitelist(std::wstring const& path);
    winrt::Windows::Foundation::IAsyncAction DeleteSelectedAsync(bool permanent);

    Core::FileNode const* CurrentNode() const noexcept { return m_current; }
    bool NeedsExpand() const;
    std::wstring StatusText() const;
    uint64_t SelectedBytes() const noexcept;
    uint32_t SelectedCount() const noexcept;

    std::atomic<bool> isScanning{ false };
    std::atomic<bool> cancelRequested{ false };
    std::atomic<bool> isDeleting{ false };
    std::atomic<uint32_t> filesFound{ 0 };
    std::atomic<uint64_t> bytesFound{ 0 };
    std::atomic<uint32_t> dirsTotal{ 0 };
    std::atomic<uint32_t> dirsDone{ 0 };
    bool truncated{ false };
    int maxDepth{ 1 };
    std::atomic<bool> previewReady{ false };
    std::atomic<bool> isOverviewSizing{ false };
    std::wstring overviewStatus;
    bool showFiles{ true };
    bool showFolders{ true };
    bool sortByName{ false };
    uint64_t groupSmallBytes{ 1024ull * 1024ull };
    std::wstring scanStatus{ L"Pick a drive and scan" };
    std::wstring lastError;
    Platform::ShellOperations::DeleteResult lastDeleteResult{};
    bool lastOperationLogged{ true };

private:
    std::unique_ptr<Core::FileNode> m_root;
    Core::FileNode* m_current{ nullptr };
    std::vector<Core::FileNode*> m_crumbs;
    Core::HardlinkTracker m_tracker;
    std::unique_ptr<Core::WhitelistStore> m_whitelist;
    std::unordered_set<std::wstring> m_selected;
    std::wstring m_filterLower;
    mutable std::mutex m_treeMutex;
    mutable std::mutex m_overviewMutex;
    std::vector<AnalyzeListItem> m_overview;
    std::atomic<uint32_t> m_overviewEpoch{ 0 };
    std::atomic<bool> m_overviewCancel{ false };

    Core::FileNode* FindChild(std::wstring const& path) const noexcept;
    Core::TreemapOptions LayoutOptions() const;
    void RemoveDeletedNodes(std::vector<std::wstring> const& paths);
};

} // namespace Orbit::ViewModels
