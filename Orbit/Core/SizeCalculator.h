#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <filesystem>
#include <cstdint>

namespace Orbit::Core
{

struct FileIdentity
{
    DWORD volumeSerial{0};
    DWORD fileIndexHigh{0};
    DWORD fileIndexLow{0};
    bool operator==(const FileIdentity& o) const noexcept
    {
        return volumeSerial == o.volumeSerial && fileIndexHigh == o.fileIndexHigh && fileIndexLow == o.fileIndexLow;
    }
};

struct FileIdentityHash
{
    size_t operator()(const FileIdentity& k) const noexcept
    {
        // Simple combine
        size_t h = static_cast<size_t>(k.volumeSerial) * 31u + k.fileIndexHigh;
        h = h * 31u + k.fileIndexLow;
        return h;
    }
};

// SizeCalculator — hardlink-aware sizing.
// Uses GetFileInformationByHandle VolumeSerial + FileIndex to deduplicate hardlinks.
// Handles reparse points (never follows directory junctions), OneDrive cloud placeholders
// (RecallOnDataAccess => 0 bytes), and compressed/sparse files via GetCompressedFileSizeW.
class SizeCalculator
{
public:
    SizeCalculator() = default;

    // Returns size of a single file (hardlink-deduped). Returns 0 on error or if
    // the file is a cloud placeholder / reparse that should not be counted.
    // Updates |seen| to track hardlink identity across a scan.
    static uint64_t FileSizeHardlinkAware(std::wstring_view path, std::unordered_set<FileIdentity, FileIdentityHash>& seen) noexcept
    {
        // Open with no access, do not follow reparse points.
        HANDLE h = ::CreateFileW(
            std::wstring(path).c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return 0;

        BY_HANDLE_FILE_INFORMATION info{};
        BOOL ok = ::GetFileInformationByHandle(h, &info);
        DWORD attrs = info.dwFileAttributes;
        // Also query attributes for reparse/recall detection if GetFileInformation failed partially
        if (!ok)
        {
            ::CloseHandle(h);
            return 0;
        }

        // OneDrive cloud placeholder: do not count (offline, not on disk)
        if ((attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0)
        {
            ::CloseHandle(h);
            return 0;
        }

        // Reparse points: size the link itself (which is tiny), but do not follow directory junctions.
        // For files we already have identity; for directory reparse we return 0 to avoid recursion loops
        // (caller directory walker will skip reparse dirs).
        if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            ::CloseHandle(h);
            return 0;
        }

        FileIdentity id{ info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow };
        // Deduplicate hardlinks: only count first occurrence.
        if (info.nNumberOfLinks > 1)
        {
            if (seen.find(id) != seen.end())
            {
                ::CloseHandle(h);
                return 0;
            }
            seen.insert(id);
        }
        else
        {
            // Still insert to handle edge where nNumberOfLinks==1 but same file visited via another path
            // (rare). Insert anyway for consistency.
            seen.insert(id);
        }

        // Prefer GetCompressedFileSizeW for sparse/CompactOS awareness; fallback to info size.
        // Need path for GetCompressedFileSizeW; use original path.
        DWORD high = 0;
        DWORD low = ::GetCompressedFileSizeW(std::wstring(path).c_str(), &high);
        uint64_t size = 0;
        if (low != INVALID_FILE_SIZE || ::GetLastError() == NO_ERROR)
        {
            // GetCompressedFileSize returns INVALID_FILE_SIZE on failure; check error
            if (low == INVALID_FILE_SIZE && ::GetLastError() != NO_ERROR)
            {
                size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            }
            else
            {
                size = (static_cast<uint64_t>(high) << 32) | low;
            }
        }
        else
        {
            size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        }

        ::CloseHandle(h);
        return size;
    }

    // Convenience overload without external seen set (single file, no cross-file dedup).
    static uint64_t FileSizeHardlinkAware(std::wstring_view path) noexcept
    {
        std::unordered_set<FileIdentity, FileIdentityHash> seen;
        return FileSizeHardlinkAware(path, seen);
    }

    // Directory size via FindFirstFileExW iteration; skips reparse dirs and protected system dirs.
    // Caller should check ProtectionList::IsProtected(root) before calling.
    static uint64_t DirectorySize(std::wstring_view root, std::unordered_set<FileIdentity, FileIdentityHash>& seen) noexcept
    {
        std::wstring pattern = std::wstring(root);
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
            pattern += L"\\";
        pattern += L"*";

        WIN32_FIND_DATAW fd{};
        HANDLE hFind = ::FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &fd,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE)
            return 0;

        uint64_t total = 0;
        do
        {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..")
                continue;

            // Skip System Volume Information and $Recycle.Bin at any depth
            if (_wcsicmp(name.c_str(), L"System Volume Information") == 0 ||
                _wcsicmp(name.c_str(), L"$Recycle.Bin") == 0)
                continue;

            // Skip reparse directories (junctions, OneDrive, WSL) to avoid loops
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                // Cloud placeholder files: count as 0, skip
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0)
                    continue;
                // Directory reparse: skip traversal
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    continue;
                // File reparse: count link itself via handle path (rare)
            }

            std::wstring full = std::wstring(root);
            if (!full.empty() && full.back() != L'\\' && full.back() != L'/')
                full += L"\\";
            full += name;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                total += DirectorySize(full, seen);
            }
            else
            {
                total += FileSizeHardlinkAware(full, seen);
            }
        } while (::FindNextFileW(hFind, &fd));
        ::FindClose(hFind);
        return total;
    }

    static uint64_t DirectorySize(std::wstring_view root) noexcept
    {
        std::unordered_set<FileIdentity, FileIdentityHash> seen;
        return DirectorySize(root, seen);
    }
};

} // namespace Orbit::Core
