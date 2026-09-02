#pragma once

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../Core/CleanCategoryRegistry.h"
#include "../Core/FileScanner.h"
#include "../Core/WhitelistStore.h"
#include "../Platform/ShellOperations.h"

namespace Orbit::ViewModels
{
    struct CleanFileItem
    {
        std::wstring path;
        uint64_t size{ 0 };
        Orbit::Core::CleanCategoryId category{
            Orbit::Core::CleanCategoryId::TempUser
        };
        Orbit::Core::CleanTier tier{ Orbit::Core::CleanTier::Safe };
        FILETIME lastWrite{};
        bool selected{ false };
        bool isHardlink{ false };
    };

    struct CleanCategoryViewModel
    {
        Orbit::Core::CleanCategoryId id{
            Orbit::Core::CleanCategoryId::TempUser
        };
        std::wstring displayName;
        std::wstring description;
        Orbit::Core::CleanTier tier{ Orbit::Core::CleanTier::Safe };
        bool recyclable{ true };
        uint64_t totalBytes{ 0 };
        uint32_t fileCount{ 0 };
        uint32_t hiddenCount{ 0 };
        uint64_t hiddenBytes{ 0 };
        std::wstring skippedReason;
        std::vector<CleanFileItem> files;
        std::vector<Orbit::Core::SkippedEntry> skipped;

        uint32_t SelectedCount() const noexcept;
        uint64_t SelectedBytes() const noexcept;
        void SelectAll(bool selected, bool allowRisky) noexcept;
    };

    class CleanViewModel
    {
    public:
        CleanViewModel();

        winrt::Windows::Foundation::IAsyncAction ScanAsync();
        void CancelScan() noexcept;
        winrt::Windows::Foundation::IAsyncAction DeleteSelectedAsync(
            bool permanent);
        winrt::Windows::Foundation::IAsyncAction EmptyRecycleBinAsync();

        bool CategoryMatchesSearch(
            const CleanCategoryViewModel& category,
            std::wstring_view searchText) const;
        void SetFileSelected(
            size_t categoryIndex,
            size_t fileIndex,
            bool selected,
            bool allowRisky);
        void SelectCategory(
            size_t categoryIndex,
            bool selected,
            bool allowRisky);
        void DeselectRisky() noexcept;
        void RecalculateSelection() noexcept;
        bool AddWhitelistPattern(
            std::wstring pattern,
            std::optional<Orbit::Core::CleanCategoryId> category = std::nullopt);
        bool WhitelistFile(size_t categoryIndex, size_t fileIndex);
        std::vector<Orbit::Core::WhitelistEntry> WhitelistEntries() const;

        bool CanScan() const noexcept;
        bool CanDelete() const noexcept;
        std::wstring TotalReclaimableFormatted() const;
        std::wstring SelectedFormatted() const;

        std::vector<CleanCategoryViewModel> categories;
        std::atomic<bool> isScanning{ false };
        std::atomic<bool> cancelRequested{ false };
        std::atomic<bool> isDeleting{ false };
        std::wstring scanStatus{ L"Ready to scan" };
        uint64_t totalReclaimableBytes{ 0 };
        uint64_t selectedBytes{ 0 };
        uint32_t totalFiles{ 0 };
        uint32_t selectedFiles{ 0 };
        Orbit::Platform::ShellOperations::DeleteResult lastDeleteResult{};
        bool lastOperationLogged{ true };

    private:
        std::unique_ptr<Orbit::Core::WhitelistStore> m_whitelist;

        void ResetScanResults() noexcept;
        void AggregateResults() noexcept;
        void RemoveDeletedFiles(
            const std::vector<std::wstring>& completedPaths) noexcept;
    };
}
