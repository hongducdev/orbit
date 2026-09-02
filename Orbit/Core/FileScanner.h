#pragma once
#include <windows.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <atomic>
#include <filesystem>
#include <cstdint>
#include <unordered_set>

#include "CleanCategoryRegistry.h"
#include "ProtectionList.h"
#include "SizeCalculator.h"
#include "WhitelistStore.h"

namespace Orbit::Core
{

struct ScannedFile
{
    std::wstring path;
    uint64_t size{ 0 };
    CleanCategoryId category{ CleanCategoryId::TempUser };
    CleanTier tier{ CleanTier::Safe };
    FILETIME lastWrite{};
    FileIdentity identity{}; // valid only if nLinks>1
    bool isHardlink{ false };
};

struct SkippedEntry
{
    std::wstring path;
    std::wstring reason;
};

struct ScanOptions
{
    bool followReparse{ false };
    bool respectWhitelist{ true };
    bool respectProtection{ true };
    int maxDepth{ 12 };
    bool skipSystemVolume{ true };
    size_t maxEntriesPerCategory{ 50000 };
};

struct ScanProgress
{
    size_t filesFound{ 0 };
    uint64_t bytesFound{ 0 };
    size_t skippedCount{ 0 };
};

// FileScanner — iterative BFS enumeration, coalesced for UI.
// Uses FindFirstFileExW(FIND_FIRST_EX_LARGE_FETCH), honors ProtectionList + Whitelist,
// hardlink-aware via SizeCalculator.
class FileScanner
{
public:
    // Synchronous scan of a single root. Checks cancelToken frequently.
    // progressCb is called for each file found (caller may coalesce).
    static std::vector<ScannedFile> ScanCategory(
        CleanCategoryId category,
        const std::vector<std::wstring>& roots,
        const ScanOptions& options,
        WhitelistStore* whitelist,
        std::unordered_set<FileIdentity, FileIdentityHash>& seen,
        std::vector<SkippedEntry>& outSkipped,
        size_t& outHiddenCount,
        uint64_t& outHiddenBytes,
        const std::atomic<bool>& cancel,
        std::function<void(const ScannedFile&)> progressCb)
    {
        std::vector<ScannedFile> out;
        out.reserve(1024);
        outHiddenCount = 0;
        outHiddenBytes = 0;
        std::wstring firstHiddenPath;
        auto tier = CleanCategoryRegistry::Tier(category);

        for (auto const& root : roots)
        {
            if (cancel.load()) break;
            if (root.empty()) continue;

            // Skip non-existent roots with SkippedEntry
            DWORD attrs = ::GetFileAttributesW(EnsureLongPath(root).c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
            {
                outSkipped.push_back({ root, L"Not present on this system" });
                continue;
            }

            // Protection check on root itself
            if (options.respectProtection && ProtectionList::IsProtected(root))
            {
                outSkipped.push_back({ root, L"Protected (system path)" });
                continue;
            }
            if ((attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0)
            {
                outSkipped.push_back({ root, L"OneDrive cloud placeholder — not selected" });
                continue;
            }
            if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && !options.followReparse)
            {
                outSkipped.push_back({ root, L"Reparse point — not selected" });
                continue;
            }

            // If root is a file (e.g., single cache file), handle directly
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                if (options.respectWhitelist && whitelist && whitelist->IsWhitelisted(root, category))
                {
                    outSkipped.push_back({ root, L"Protected (whitelisted)" });
                    continue;
                }
                auto measured = SizeCalculator::MeasureFile(root, seen);
                ScannedFile file;
                file.path = root;
                file.size = measured.size;
                file.category = category;
                file.tier = IsRiskyTempBinary(category, root) ? CleanTier::Risky : tier;
                file.identity = measured.identity;
                file.isHardlink = measured.isHardlink;
                out.push_back(std::move(file));
                if (progressCb) progressCb(out.back());
                continue;
            }

            // Iterative stack for BFS/DFS
            struct StackEntry { std::wstring dir; int depth; };
            std::vector<StackEntry> stack;
            stack.reserve(64);
            stack.push_back({ root, 0 });

            while (!stack.empty() && !cancel.load())
            {
                StackEntry cur = std::move(stack.back());
                stack.pop_back();

                if (cur.depth > options.maxDepth)
                {
                    outSkipped.push_back({ cur.dir, L"Max depth reached" });
                    continue;
                }

                if (options.respectProtection && ProtectionList::IsProtected(cur.dir))
                {
                    outSkipped.push_back({ cur.dir, L"Protected (system path)" });
                    continue;
                }

                std::wstring pattern = cur.dir;
                if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L"\\";
                pattern += L"*";
                std::wstring probePattern = EnsureLongPath(pattern);

                WIN32_FIND_DATAW fd{};
                HANDLE hFind = ::FindFirstFileExW(
                    probePattern.c_str(), FindExInfoBasic, &fd,
                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
                if (hFind == INVALID_HANDLE_VALUE)
                {
                    DWORD err = ::GetLastError();
                    if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
                        outSkipped.push_back({ cur.dir, L"Access denied or enumeration failed" });
                    continue;
                }

                do
                {
                    if (cancel.load()) break;
                    std::wstring name = fd.cFileName;
                    if (name == L"." || name == L"..") continue;
                    if (options.skipSystemVolume)
                    {
                        if (_wcsicmp(name.c_str(), L"System Volume Information") == 0) continue;
                        if (_wcsicmp(name.c_str(), L"$Recycle.Bin") == 0) continue;
                    }

                    std::wstring full = cur.dir;
                    if (!full.empty() && full.back() != L'\\' && full.back() != L'/') full += L"\\";
                    full += name;

                    bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    bool isReparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    bool isRecall = (fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;

                    // OneDrive placeholders: never auto-select, but we record as skipped with reason
                    if (isRecall)
                    {
                        outSkipped.push_back({ full, L"OneDrive cloud placeholder — not selected" });
                        continue;
                    }
                    // Reparse points
                    if (isReparse && !options.followReparse)
                    {
                        // Skip directory junctions/symlinks
                        if (isDir) continue;
                        // For reparse files, skip as well to avoid double counting
                        continue;
                    }

                    if (isDir)
                    {
                        if (options.respectProtection && ProtectionList::IsProtected(full))
                        {
                            outSkipped.push_back({ full, L"Protected" });
                            continue;
                        }
                        if (options.respectWhitelist && whitelist && whitelist->IsWhitelisted(full, category))
                        {
                            outSkipped.push_back({ full, L"Protected (whitelisted)" });
                            continue;
                        }
                        stack.push_back({ full, cur.depth + 1 });
                    }
                    else
                    {
                        if (options.respectProtection && ProtectionList::IsProtected(full))
                        {
                            outSkipped.push_back({ full, L"Protected" });
                            continue;
                        }
                        if (options.respectWhitelist && whitelist && whitelist->IsWhitelisted(full, category))
                        {
                            outSkipped.push_back({ full, L"Protected (whitelisted)" });
                            continue;
                        }
                        if (out.size() >= options.maxEntriesPerCategory)
                        {
                            if (firstHiddenPath.empty()) firstHiddenPath = full;
                            ::FindClose(hFind);
                            stack.clear();
                            goto scan_complete_for_root;
                        }
                        uint64_t fallbackSize =
                            (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) |
                            fd.nFileSizeLow;
                        auto measured = SizeCalculator::MeasureFile(full, seen, fallbackSize);

                        ScannedFile file;
                        file.path = full;
                        file.size = measured.size;
                        file.category = category;
                        file.tier = IsRiskyTempBinary(category, full) ? CleanTier::Risky : tier;
                        file.lastWrite = fd.ftLastWriteTime;
                        file.identity = measured.identity;
                        file.isHardlink = measured.isHardlink;
                        out.push_back(std::move(file));
                        if (progressCb) progressCb(out.back());
                    }
                } while (::FindNextFileW(hFind, &fd));
                ::FindClose(hFind);
            }
scan_complete_for_root:;
        }
        if (outHiddenCount > 0 || !firstHiddenPath.empty())
        {
            outSkipped.push_back({
                firstHiddenPath,
                L"Category cap reached — " + std::to_wstring(outHiddenCount) +
                    L" additional files summarized"
            });
        }
        return out;
    }

private:
    static bool IsRiskyTempBinary(
        CleanCategoryId category,
        const std::wstring& path) noexcept
    {
        if (category != CleanCategoryId::TempUser &&
            category != CleanCategoryId::TempSystem)
        {
            return false;
        }
        std::wstring extension = std::filesystem::path(path).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        return extension == L".exe" || extension == L".dll";
    }

    static std::wstring EnsureLongPath(const std::wstring& s)
    {
        if (s.rfind(L"\\\\?\\", 0) == 0 || s.rfind(L"\\\\.\\", 0) == 0) return s;
        if (s.size() >= 260)
        {
            if (s.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + s.substr(2);
            return L"\\\\?\\" + s;
        }
        return s;
    }
};

} // namespace Orbit::Core
