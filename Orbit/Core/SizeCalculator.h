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

struct FileSizeResult
{
    uint64_t size{ 0 };
    FileIdentity identity{};
    bool isHardlink{ false };
    bool duplicateHardlink{ false };
    bool usedFallback{ false };
};

// SizeCalculator — hardlink-aware sizing. Hardlink dedup only for nNumberOfLinks>1 to bound memory.
class SizeCalculator
{
public:
    static constexpr int kMaxDepth = 256;

    static FileSizeResult MeasureFile(
        std::wstring_view path,
        std::unordered_set<FileIdentity, FileIdentityHash>& seen,
        uint64_t fallbackSize = 0) noexcept
    {
        FileSizeResult result;
        std::wstring probePath = EnsureLongPath(std::wstring(path));
        HANDLE handle = ::CreateFileW(
            probePath.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);

        if (handle == INVALID_HANDLE_VALUE)
        {
            result.size = fallbackSize ? fallbackSize : LogicalFileSize(probePath);
            result.usedFallback = true;
            return result;
        }

        BY_HANDLE_FILE_INFORMATION info{};
        if (!::GetFileInformationByHandle(handle, &info))
        {
            ::CloseHandle(handle);
            result.size = fallbackSize ? fallbackSize : LogicalFileSize(probePath);
            result.usedFallback = true;
            return result;
        }

        if ((info.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0 ||
            ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
             (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
        {
            ::CloseHandle(handle);
            return result;
        }

        if (info.nNumberOfLinks > 1)
        {
            result.isHardlink = true;
            result.identity = {
                info.dwVolumeSerialNumber,
                info.nFileIndexHigh,
                info.nFileIndexLow
            };
            if (!seen.insert(result.identity).second)
            {
                result.duplicateHardlink = true;
                ::CloseHandle(handle);
                return result;
            }
        }

        ::SetLastError(NO_ERROR);
        DWORD high = 0;
        DWORD low = ::GetCompressedFileSizeW(probePath.c_str(), &high);
        DWORD error = ::GetLastError();
        if (low == INVALID_FILE_SIZE && error != NO_ERROR)
        {
            result.size =
                (static_cast<uint64_t>(info.nFileSizeHigh) << 32) |
                info.nFileSizeLow;
            result.usedFallback = true;
        }
        else
        {
            result.size = (static_cast<uint64_t>(high) << 32) | low;
        }

        ::CloseHandle(handle);
        return result;
    }

    static uint64_t FileSizeHardlinkAware(
        std::wstring_view path,
        std::unordered_set<FileIdentity, FileIdentityHash>& seen) noexcept
    {
        return MeasureFile(path, seen).size;
    }

    static uint64_t FileSizeHardlinkAware(std::wstring_view path) noexcept
    {
        std::unordered_set<FileIdentity, FileIdentityHash> seen;
        return MeasureFile(path, seen).size;
    }

    static uint64_t DirectorySize(
        std::wstring_view root,
        std::unordered_set<FileIdentity, FileIdentityHash>& seen,
        int depth = 0) noexcept
    {
        if (depth > kMaxDepth)
        {
            return 0;
        }

        std::wstring rootPath(root);
        std::wstring pattern = rootPath;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
        {
            pattern += L"\\";
        }
        pattern = EnsureLongPath(pattern + L"*");

        WIN32_FIND_DATAW data{};
        HANDLE findHandle = ::FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        uint64_t total = 0;
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L".." ||
                _wcsicmp(name.c_str(), L"System Volume Information") == 0 ||
                _wcsicmp(name.c_str(), L"$Recycle.Bin") == 0)
            {
                continue;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                continue;
            }

            std::wstring fullPath = rootPath;
            if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/')
            {
                fullPath += L"\\";
            }
            fullPath += name;

            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                total += DirectorySize(fullPath, seen, depth + 1);
            }
            else
            {
                uint64_t fallback =
                    (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
                    data.nFileSizeLow;
                total += MeasureFile(fullPath, seen, fallback).size;
            }
        } while (::FindNextFileW(findHandle, &data));

        ::FindClose(findHandle);
        return total;
    }

    static uint64_t DirectorySize(std::wstring_view root) noexcept
    {
        std::unordered_set<FileIdentity, FileIdentityHash> seen;
        return DirectorySize(root, seen, 0);
    }

private:
    static uint64_t LogicalFileSize(const std::wstring& path) noexcept
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            return 0;
        }
        return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
            data.nFileSizeLow;
    }

    static std::wstring EnsureLongPath(const std::wstring& path)
    {
        if (path.rfind(L"\\\\?\\", 0) == 0 || path.rfind(L"\\\\.\\", 0) == 0)
        {
            return path;
        }
        if (path.size() >= MAX_PATH)
        {
            if (path.rfind(L"\\\\", 0) == 0)
            {
                return L"\\\\?\\UNC\\" + path.substr(2);
            }
            return L"\\\\?\\" + path;
        }
        return path;
    }
};

} // namespace Orbit::Core
