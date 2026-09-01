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
        size_t h = static_cast<size_t>(k.volumeSerial) * 31u + k.fileIndexHigh;
        h = h * 31u + k.fileIndexLow;
        return h;
    }
};

// SizeCalculator — hardlink-aware sizing. Hardlink dedup only for nNumberOfLinks>1 to bound memory.
class SizeCalculator
{
public:
    static constexpr int kMaxDepth = 256;

    static uint64_t FileSizeHardlinkAware(std::wstring_view path, std::unordered_set<FileIdentity, FileIdentityHash>& seen) noexcept
    {
        std::wstring wpath(path);
        // Long-path support: prefix \\?\ if needed and not already present
        std::wstring probePath = EnsureLongPath(wpath);

        HANDLE h = ::CreateFileW(
            probePath.c_str(),
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
        if (!ok)
        {
            ::CloseHandle(h);
            return 0;
        }
        DWORD attrs = info.dwFileAttributes;
        if ((attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0)
        {
            ::CloseHandle(h);
            return 0;
        }
        if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            ::CloseHandle(h);
            return 0;
        }

        // Only track hardlinks — bound seen set (H1 fix)
        if (info.nNumberOfLinks > 1)
        {
            FileIdentity id{ info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow };
            if (seen.find(id) != seen.end())
            {
                ::CloseHandle(h);
                return 0;
            }
            seen.insert(id);
        }

        DWORD high = 0;
        DWORD low = ::GetCompressedFileSizeW(probePath.c_str(), &high);
        DWORD err = ::GetLastError(); // cache once (H3 fix)
        uint64_t size = 0;
        if (low == INVALID_FILE_SIZE && err != NO_ERROR)
        {
            size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        }
        else if (low == INVALID_FILE_SIZE && err == NO_ERROR)
        {
            // File is exactly 0xFFFFFFFF bytes compressed — valid, size is high:low
            size = (static_cast<uint64_t>(high) << 32) | low;
        }
        else
        {
            // low != INVALID_FILE_SIZE -> high is valid
            size = (static_cast<uint64_t>(high) << 32) | low;
        }

        ::CloseHandle(h);
        return size;
    }

    static uint64_t FileSizeHardlinkAware(std::wstring_view path) noexcept
    {
        std::unordered_set<FileIdentity, FileIdentityHash> seen;
        return FileSizeHardlinkAware(path, seen);
    }

    static uint64_t DirectorySize(std::wstring_view root, std::unordered_set<FileIdentity, FileIdentityHash>& seen, int depth = 0) noexcept
    {
        if (depth > kMaxDepth) return 0; // H5: bound recursion

        std::wstring wroot(root);
        std::wstring pattern = wroot;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
            pattern += L"\\";
        pattern += L"*";
        pattern = EnsureLongPath(pattern);

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
            if (_wcsicmp(name.c_str(), L"System Volume Information") == 0 ||
                _wcsicmp(name.c_str(), L"$Recycle.Bin") == 0)
                continue;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0)
                    continue;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    continue;
            }

            std::wstring full = wroot;
            if (!full.empty() && full.back() != L'\\' && full.back() != L'/')
                full += L"\\";
            full += name;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                total += DirectorySize(full, seen, depth + 1);
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
        return DirectorySize(root, seen, 0);
    }

private:
    static std::wstring EnsureLongPath(const std::wstring& s)
    {
        if (s.rfind(L"\\\\?\\", 0) == 0 || s.rfind(L"\\\\.\\", 0) == 0)
            return s;
        if (s.size() >= 260)
        {
            if (s.rfind(L"\\\\", 0) == 0)
                return L"\\\\?\\UNC\\" + s.substr(2);
            return L"\\\\?\\" + s;
        }
        return s;
    }
};

} // namespace Orbit::Core
