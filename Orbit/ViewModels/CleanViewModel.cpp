#include "pch.h"
#include "CleanViewModel.h"

#include "../Core/CleanCategoryProber.h"
#include "../Core/OperationLog.h"

#include <algorithm>
#include <filesystem>
#include <future>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::wstring Lowercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), ::towlower);
        return value;
    }

    bool MatchesThumbCachePattern(const std::wstring& path)
    {
        std::wstring name = Lowercase(std::filesystem::path(path).filename().wstring());
        return name.rfind(L"thumbcache_", 0) == 0 &&
            name.size() > 3 && name.ends_with(L".db");
    }
}

namespace Orbit::ViewModels
{
    uint32_t CleanCategoryViewModel::SelectedCount() const noexcept
    {
        return static_cast<uint32_t>(std::count_if(
            files.begin(), files.end(), [](auto const& file) { return file.selected; }));
    }

    uint64_t CleanCategoryViewModel::SelectedBytes() const noexcept
    {
        uint64_t bytes = 0;
        for (auto const& file : files)
        {
            if (file.selected) bytes += file.size;
        }
        return bytes;
    }

    void CleanCategoryViewModel::SelectAll(bool selected, bool allowRisky) noexcept
    {
        for (auto& file : files)
        {
            if (selected && file.tier == Core::CleanTier::Risky && !allowRisky)
            {
                continue;
            }
            file.selected = selected && recyclable;
        }
    }

    CleanViewModel::CleanViewModel() :
        m_whitelist(std::make_unique<Core::WhitelistStore>())
    {
        for (auto const& metadata : Core::CleanCategoryRegistry::All())
        {
            CleanCategoryViewModel category;
            category.id = metadata.id;
            category.displayName = metadata.name;
            category.description = metadata.description;
            category.tier = metadata.tier;
            category.recyclable = metadata.recyclable;
            categories.push_back(std::move(category));
        }
    }

    winrt::Windows::Foundation::IAsyncAction CleanViewModel::ScanAsync()
    {
        bool expected = false;
        if (!isScanning.compare_exchange_strong(expected, true) || isDeleting.load())
        {
            co_return;
        }

        cancelRequested.store(false);
        scanStatus = L"Scanning caches…";
        ResetScanResults();
        winrt::apartment_context uiThread;
        std::wstring scanError;

        co_await winrt::resume_background();
        try
        {
            Core::ScanOptions options;
            std::unordered_set<Core::FileIdentity, Core::FileIdentityHash> seenFiles;

            for (auto& category : categories)
            {
                if (cancelRequested.load()) break;

                if (category.id == Core::CleanCategoryId::RecycleBin)
                {
                    category.totalBytes = Platform::ShellOperations::GetRecycleBinSizeBytes();
                    category.fileCount = Platform::ShellOperations::GetRecycleBinItemCount();
                    if (category.fileCount > 0)
                    {
                        category.files.push_back({
                            L"Recycle Bin (all drives)",
                            category.totalBytes,
                            category.id,
                            category.tier,
                            {},
                            false,
                            false
                        });
                    }
                    else
                    {
                        category.skippedReason = L"Recycle Bin is empty";
                    }
                    continue;
                }

                auto roots = Core::CleanCategoryProber::Probe(category.id);
                if (roots.empty())
                {
                    category.skippedReason = L"Not present on this system";
                    continue;
                }

                std::vector<Core::SkippedEntry> skipped;
                auto scanned = Core::FileScanner::ScanCategory(
                    category.id,
                    roots,
                    options,
                    m_whitelist.get(),
                    seenFiles,
                    skipped,
                    category.hiddenCount,
                    category.hiddenBytes,
                    cancelRequested,
                    nullptr);

                std::wstring pattern;
                if (Core::CleanCategoryProber::FilePattern(category.id, pattern))
                {
                    std::erase_if(scanned, [](auto const& file) {
                        return !MatchesThumbCachePattern(file.path);
                    });
                }

                category.files.reserve(scanned.size());
                for (auto& scannedFile : scanned)
                {
                    bool selected = category.recyclable &&
                        scannedFile.tier != Core::CleanTier::Risky;
                    category.files.push_back({
                        std::move(scannedFile.path),
                        scannedFile.size,
                        scannedFile.category,
                        scannedFile.tier,
                        scannedFile.lastWrite,
                        selected,
                        scannedFile.isHardlink
                    });
                }
                std::sort(
                    category.files.begin(),
                    category.files.end(),
                    [](auto const& left, auto const& right) {
                        return left.size > right.size;
                    });
                category.skipped = std::move(skipped);
                category.fileCount = static_cast<uint32_t>(category.files.size());
                category.totalBytes = category.hiddenBytes;
                for (auto const& file : category.files)
                {
                    category.totalBytes += file.size;
                }
            }
            AggregateResults();
        }
        catch (std::exception const& error)
        {
            auto message = winrt::to_hstring(error.what());
            scanError.assign(message.c_str(), message.size());
        }
        catch (...)
        {
            scanError = L"Unexpected scanner failure";
        }

        co_await uiThread;
        isScanning.store(false);
        if (!scanError.empty())
        {
            scanStatus = L"Scan failed — " + scanError;
        }
        else if (cancelRequested.load())
        {
            scanStatus = L"Cancelled — partial results shown";
        }
        else
        {
            scanStatus = L"Scan complete — review selections before cleaning";
        }
    }

    void CleanViewModel::CancelScan() noexcept
    {
        if (!isScanning.load()) return;
        cancelRequested.store(true);
        scanStatus = L"Cancelling…";
    }

    winrt::Windows::Foundation::IAsyncAction CleanViewModel::DeleteSelectedAsync(
        bool permanent)
    {
        bool expected = false;
        if (!isDeleting.compare_exchange_strong(expected, true) || isScanning.load())
        {
            lastDeleteResult = { false, E_PENDING, 0, 0, L"Another operation is active" };
            co_return;
        }

        std::vector<std::wstring> filesystemPaths;
        std::unordered_map<std::wstring, uint64_t> selectedSizes;
        for (auto const& category : categories)
        {
            for (auto const& file : category.files)
            {
                if (!file.selected) continue;
                filesystemPaths.push_back(file.path);
                selectedSizes.emplace(file.path, file.size);
            }
        }

        if (filesystemPaths.empty())
        {
            lastDeleteResult = { false, S_FALSE, 0, 0, L"No files selected" };
            isDeleting.store(false);
            co_return;
        }

        winrt::apartment_context uiThread;
        co_await winrt::resume_background();

        Platform::ShellOperations::DeleteResult result;
        try
        {
            auto deletion = std::async(
                std::launch::async,
                [paths = std::move(filesystemPaths), permanent]() {
                    return Platform::ShellOperations::DeleteFiles(paths, permanent);
                });
            result = deletion.get();
        }
        catch (...)
        {
            result = {
                false,
                E_FAIL,
                selectedSizes.size(),
                0,
                L"Windows file deletion failed unexpectedly"
            };
        }

        Core::OperationLogEntry entry;
        entry.timestampIso8601 = Core::OperationLog::NowIso8601();
        entry.op = Core::OperationKind::Clean;
        entry.category = L"clean";
        entry.paths = result.completedPaths;
        if (entry.paths.empty())
        {
            entry.paths.reserve(selectedSizes.size());
            for (auto const& [path, size] : selectedSizes)
            {
                static_cast<void>(size);
                entry.paths.push_back(path);
            }
        }
        for (auto const& path : entry.paths)
        {
            auto found = selectedSizes.find(path);
            if (found != selectedSizes.end()) entry.bytesBefore += found->second;
        }
        entry.dest = permanent ? Core::Destination::Permanent : Core::Destination::Recycle;
        entry.outcome = result.succeeded
            ? Core::OperationOutcome::Success
            : (result.IsPartial() ? Core::OperationOutcome::Partial : Core::OperationOutcome::Failed);
        entry.error = result.error;
        bool operationLogged = Core::OperationLog::Append(entry);

        co_await uiThread;
        lastOperationLogged = operationLogged;
        lastDeleteResult = result;
        if (!result.completedPaths.empty())
        {
            RemoveDeletedFiles(result.completedPaths);
        }
        if (result.succeeded)
        {
            scanStatus = permanent
                ? L"Selected files were permanently deleted"
                : L"Selected files were moved to the Recycle Bin";
        }
        else
        {
            scanStatus = result.error.empty() ? L"Clean operation failed" : result.error;
        }
        if (!lastOperationLogged)
        {
            scanStatus += L" — history log could not be written";
        }
        isDeleting.store(false);
    }

    winrt::Windows::Foundation::IAsyncAction CleanViewModel::EmptyRecycleBinAsync()
    {
        bool expected = false;
        if (!isDeleting.compare_exchange_strong(expected, true) || isScanning.load())
        {
            lastDeleteResult = { false, E_PENDING, 0, 0, L"Another operation is active" };
            co_return;
        }

        uint64_t bytesBefore = 0;
        for (auto const& category : categories)
        {
            if (category.id == Core::CleanCategoryId::RecycleBin)
            {
                bytesBefore = category.totalBytes;
                break;
            }
        }

        winrt::apartment_context uiThread;
        co_await winrt::resume_background();
        Platform::ShellOperations::DeleteResult result;
        try
        {
            auto deletion = std::async(
                std::launch::async,
                []() { return Platform::ShellOperations::EmptyRecycleBin(); });
            result = deletion.get();
        }
        catch (...)
        {
            result = {
                false,
                E_FAIL,
                1,
                0,
                L"Emptying the Recycle Bin failed unexpectedly"
            };
        }

        Core::OperationLogEntry entry;
        entry.timestampIso8601 = Core::OperationLog::NowIso8601();
        entry.op = Core::OperationKind::Clean;
        entry.category = L"recycle-bin";
        entry.paths = { L"Recycle Bin (all drives)" };
        entry.bytesBefore = bytesBefore;
        entry.dest = Core::Destination::Permanent;
        entry.outcome = result.succeeded
            ? Core::OperationOutcome::Success
            : Core::OperationOutcome::Failed;
        entry.error = result.error;
        bool operationLogged = Core::OperationLog::Append(entry);

        co_await uiThread;
        lastOperationLogged = operationLogged;
        lastDeleteResult = result;
        if (result.succeeded)
        {
            for (auto& category : categories)
            {
                if (category.id != Core::CleanCategoryId::RecycleBin) continue;
                category.totalBytes = 0;
                category.fileCount = 0;
                category.files.clear();
                category.skippedReason = L"Recycle Bin is empty";
                break;
            }
            AggregateResults();
            scanStatus = L"Recycle Bin emptied";
        }
        else
        {
            scanStatus = result.error;
        }
        if (!lastOperationLogged)
        {
            scanStatus += L" — history log could not be written";
        }
        isDeleting.store(false);
    }

    bool CleanViewModel::CategoryMatchesSearch(
        const CleanCategoryViewModel& category,
        std::wstring_view searchText) const
    {
        if (searchText.empty()) return true;
        std::wstring needle = Lowercase(std::wstring(searchText));
        if (Lowercase(category.displayName).find(needle) != std::wstring::npos)
        {
            return true;
        }
        return std::any_of(category.files.begin(), category.files.end(), [&](auto const& file) {
            return Lowercase(file.path).find(needle) != std::wstring::npos;
        });
    }

    void CleanViewModel::SetFileSelected(
        size_t categoryIndex,
        size_t fileIndex,
        bool selected,
        bool allowRisky)
    {
        if (categoryIndex >= categories.size()) return;
        auto& category = categories[categoryIndex];
        if (fileIndex >= category.files.size() || !category.recyclable) return;
        auto& file = category.files[fileIndex];
        if (selected && file.tier == Core::CleanTier::Risky && !allowRisky) return;
        file.selected = selected;
        RecalculateSelection();
    }

    void CleanViewModel::SelectCategory(
        size_t categoryIndex,
        bool selected,
        bool allowRisky)
    {
        if (categoryIndex >= categories.size()) return;
        categories[categoryIndex].SelectAll(selected, allowRisky);
        RecalculateSelection();
    }
    void CleanViewModel::DeselectRisky() noexcept
    {
        for (auto& category : categories)
        {
            for (auto& file : category.files)
            {
                if (file.tier == Core::CleanTier::Risky)
                {
                    file.selected = false;
                }
            }
        }
        RecalculateSelection();
    }


    void CleanViewModel::RecalculateSelection() noexcept
    {
        selectedBytes = 0;
        selectedFiles = 0;
        for (auto const& category : categories)
        {
            selectedBytes += category.SelectedBytes();
            selectedFiles += category.SelectedCount();
        }
    }

    bool CleanViewModel::AddWhitelistPattern(
        std::wstring pattern,
        std::optional<Core::CleanCategoryId> category)
    {
        return m_whitelist->Add(std::move(pattern), category);
    }

    bool CleanViewModel::WhitelistFile(size_t categoryIndex, size_t fileIndex)
    {
        if (categoryIndex >= categories.size()) return false;
        auto& category = categories[categoryIndex];
        if (fileIndex >= category.files.size()) return false;
        auto path = category.files[fileIndex].path;
        if (!m_whitelist->Add(path, category.id)) return false;
        category.skipped.push_back({ path, L"Protected (whitelisted)" });
        category.files.erase(category.files.begin() + fileIndex);
        category.fileCount = static_cast<uint32_t>(category.files.size());
        category.totalBytes = category.hiddenBytes;
        for (auto const& file : category.files) category.totalBytes += file.size;
        AggregateResults();
        return true;
    }

    std::vector<Core::WhitelistEntry> CleanViewModel::WhitelistEntries() const
    {
        return m_whitelist->Entries();
    }

    bool CleanViewModel::CanScan() const noexcept
    {
        return !isScanning.load() && !isDeleting.load();
    }

    bool CleanViewModel::CanDelete() const noexcept
    {
        return CanScan() && selectedFiles > 0;
    }

    std::wstring CleanViewModel::TotalReclaimableFormatted() const
    {
        return Platform::ShellOperations::FormatBytes(totalReclaimableBytes);
    }

    std::wstring CleanViewModel::SelectedFormatted() const
    {
        return Platform::ShellOperations::FormatBytes(selectedBytes);
    }

    void CleanViewModel::ResetScanResults() noexcept
    {
        for (auto& category : categories)
        {
            category.totalBytes = 0;
            category.fileCount = 0;
            category.hiddenCount = 0;
            category.hiddenBytes = 0;
            category.skippedReason.clear();
            category.files.clear();
            category.skipped.clear();
        }
        totalReclaimableBytes = 0;
        selectedBytes = 0;
        totalFiles = 0;
        selectedFiles = 0;
    }

    void CleanViewModel::AggregateResults() noexcept
    {
        totalReclaimableBytes = 0;
        totalFiles = 0;
        for (auto const& category : categories)
        {
            totalReclaimableBytes += category.totalBytes;
            totalFiles += category.fileCount + category.hiddenCount;
        }
        RecalculateSelection();
    }

    void CleanViewModel::RemoveDeletedFiles(
        const std::vector<std::wstring>& completedPaths) noexcept
    {
        std::unordered_set<std::wstring> completed(
            completedPaths.begin(),
            completedPaths.end());
        for (auto& category : categories)
        {
            std::erase_if(category.files, [&](auto const& file) {
                return completed.contains(file.path);
            });
            category.fileCount = static_cast<uint32_t>(category.files.size());
            category.totalBytes = category.hiddenBytes;
            for (auto const& file : category.files) category.totalBytes += file.size;
        }
        AggregateResults();
    }
}
