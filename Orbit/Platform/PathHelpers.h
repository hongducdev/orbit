#pragma once
#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <cwctype>
#include <vector>

#ifndef IO_REPARSE_TAG_CLOUD
#define IO_REPARSE_TAG_CLOUD 0x9000001AL
#endif

namespace Orbit::Platform
{

#ifndef ORBIT_REPARSE_DATA_BUFFER_DEFINED
#define ORBIT_REPARSE_DATA_BUFFER_DEFINED
typedef struct _ORBIT_REPARSE_DATA_BUFFER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union
    {
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
    } DUMMYUNIONNAME;
} ORBIT_REPARSE_DATA_BUFFER;
#endif

class PathHelpers
{
public:
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

    static bool IsSystemVolumeInfo(std::wstring_view name) noexcept
    {
        return _wcsicmp(std::wstring(name).c_str(), L"System Volume Information") == 0 ||
            _wcsicmp(std::wstring(name).c_str(), L"$Recycle.Bin") == 0;
    }

    static bool IsOneDrivePlaceholder(DWORD attributes) noexcept
    {
        constexpr DWORD kRecallOnOpen = 0x00040000;
        return (attributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0 ||
            (attributes & kRecallOnOpen) != 0;
    }

    static bool IsReparseJunction(DWORD attributes) noexcept
    {
        return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    static std::wstring Join(std::wstring_view parent, std::wstring_view name)
    {
        std::wstring full(parent);
        if (!full.empty() && full.back() != L'\\' && full.back() != L'/')
        {
            full += L'\\';
        }
        full.append(name);
        return full;
    }

    static std::optional<std::wstring> ReadReparseTarget(const std::wstring& path)
    {
        HANDLE handle = ::CreateFileW(
            EnsureLongPath(path).c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return std::nullopt;
        }

        std::vector<BYTE> buffer(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
        DWORD bytesReturned = 0;
        BOOL ok = ::DeviceIoControl(
            handle,
            FSCTL_GET_REPARSE_POINT,
            nullptr,
            0,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytesReturned,
            nullptr);
        ::CloseHandle(handle);
        if (!ok)
        {
            return std::nullopt;
        }

        auto* reparse = reinterpret_cast<ORBIT_REPARSE_DATA_BUFFER*>(buffer.data());
        if (reparse->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT &&
            reparse->ReparseTag != IO_REPARSE_TAG_SYMLINK)
        {
            return std::nullopt;
        }

        USHORT offset = 0;
        USHORT length = 0;
        WCHAR const* pathBuffer = nullptr;
        if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK)
        {
            offset = reparse->SymbolicLinkReparseBuffer.SubstituteNameOffset;
            length = reparse->SymbolicLinkReparseBuffer.SubstituteNameLength;
            pathBuffer = reparse->SymbolicLinkReparseBuffer.PathBuffer;
        }
        else
        {
            offset = reparse->MountPointReparseBuffer.SubstituteNameOffset;
            length = reparse->MountPointReparseBuffer.SubstituteNameLength;
            pathBuffer = reparse->MountPointReparseBuffer.PathBuffer;
        }
        std::wstring target(pathBuffer + (offset / sizeof(WCHAR)), length / sizeof(WCHAR));
        if (target.rfind(L"\\??\\", 0) == 0)
        {
            target = target.substr(4);
        }
        return target;
    }

    static bool IsPathInsideRoot(std::wstring_view path, std::wstring_view root)
    {
        std::wstring left = ToLower(std::wstring(path));
        std::wstring right = ToLower(std::wstring(root));
        while (!left.empty() && (left.back() == L'\\' || left.back() == L'/')) left.pop_back();
        while (!right.empty() && (right.back() == L'\\' || right.back() == L'/')) right.pop_back();
        if (left.size() < right.size()) return false;
        if (left.compare(0, right.size(), right) != 0) return false;
        return left.size() == right.size() || left[right.size()] == L'\\';
    }

    static bool RevealInExplorer(std::wstring_view path)
    {
        std::wstring escaped(path);
        escaped.erase(
            std::remove(escaped.begin(), escaped.end(), L'"'),
            escaped.end());
        if (escaped.empty())
        {
            return false;
        }
        std::wstring params = L"/select,\"" + escaped + L"\"";
        HINSTANCE result = ::ShellExecuteW(
            nullptr,
            L"open",
            L"explorer.exe",
            params.c_str(),
            nullptr,
            SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

private:
    static std::wstring ToLower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), ::towlower);
        return value;
    }
};

} // namespace Orbit::Platform
