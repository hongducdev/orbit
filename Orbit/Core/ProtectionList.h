#pragma once
#include <windows.h>
#include <shlwapi.h>
#include <pathcch.h>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace Orbit::Core
{

// ProtectionList — denylist for paths that must never be auto-deleted.
// Drive-agnostic: derives system drive/users prefix at runtime, canonicalizes .. segments.
class ProtectionList
{
public:
    static bool IsProtected(std::wstring_view path) noexcept
    {
        if (path.empty()) return true;

        std::wstring norm = Normalize(path);
        std::wstring canon = Canonicalize(norm);
        std::wstring lower = ToLower(canon);

        // Build denied suffixes relative to any drive (e.g., \windows, \windows\system32).
        // Check suffix after drive root X:\ or UNC \\server\share\.
        std::wstring suffix = SuffixAfterRoot(lower);
        // Denied suffixes — subtree protected.
        static const std::wstring kDeniedSuffixes[] = {
            L"\\windows\\system32",
            L"\\windows\\winsxs",
            L"\\windows\\systemapps",
            L"\\windows\\servicing",
            L"\\program files\\windowsapps",
            L"\\system volume information",
            L"\\$recycle.bin",
            L"\\recovery",
        };
        for (auto const& s : kDeniedSuffixes)
        {
            if (IsPrefixOrEqual(suffix, s))
                return true;
        }

        if (IsUserProfileRoot(lower))
            return true;
        if (IsInsideUserDataRoot(lower))
            return true;

        return false;
    }

    static bool IsReparseProtected(DWORD attrs) noexcept
    {
        return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    static bool IsRecallOnDataAccess(DWORD attrs) noexcept
    {
        return (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
    }

private:
    static std::wstring Normalize(std::wstring_view sv)
    {
        std::wstring s(sv);
        // Handle \\?\ and \\?\UNC\ prefixes per H4
        const std::wstring longPrefix = L"\\\\?\\";
        const std::wstring uncPrefix = L"\\\\?\\UNC\\";
        if (s.size() >= uncPrefix.size() && s.compare(0, uncPrefix.size(), uncPrefix) == 0)
        {
            // \\?\UNC\server\share\path -> \\server\share\path
            s = L"\\\\" + s.substr(uncPrefix.size());
        }
        else if (s.size() >= longPrefix.size() && s.compare(0, longPrefix.size(), longPrefix) == 0)
        {
            s = s.substr(longPrefix.size());
        }
        // \\.\ is device namespace; strip similarly
        const std::wstring devPrefix = L"\\\\.\\";
        if (s.size() >= devPrefix.size() && s.compare(0, devPrefix.size(), devPrefix) == 0)
            s = s.substr(devPrefix.size());

        std::replace(s.begin(), s.end(), L'/', L'\\');
        while (s.size() > 3 && s.back() == L'\\')
            s.pop_back();
        return s;
    }

    static std::wstring Canonicalize(const std::wstring& s)
    {
        wchar_t canon[MAX_PATH * 2]{};
        HRESULT hr = ::PathCchCanonicalizeEx(canon, MAX_PATH * 2, s.c_str(), PATHCCH_ALLOW_LONG_PATHS);
        if (SUCCEEDED(hr))
            return std::wstring(canon);
        // fallback: GetFullPathNameW handles .. segments for non-long paths
        wchar_t full[MAX_PATH * 2]{};
        DWORD len = ::GetFullPathNameW(s.c_str(), MAX_PATH * 2, full, nullptr);
        if (len > 0 && len < MAX_PATH * 2)
            return std::wstring(full);
        return s;
    }

    // Returns suffix after drive root or UNC share, e.g., C:\Windows\System32 -> \windows\system32
    // UNC \\server\share\foo -> \foo ; if no root, returns lower itself
    static std::wstring SuffixAfterRoot(const std::wstring& lower) noexcept
    {
        // A \\?\Volume{GUID}\ path loses its namespace prefix during normalization.
        // Treat the portion after the volume identifier as the drive-relative suffix.
        size_t volumePos = lower.find(L"volume{");
        if (volumePos != std::wstring::npos)
        {
            size_t volumeEnd = lower.find(L"}\\", volumePos);
            if (volumeEnd != std::wstring::npos)
            {
                return lower.substr(volumeEnd + 1);
            }
        }

        if (lower.size() >= 2 && lower[1] == L':')
        {
            // Drive-rooted: X:\...
            if (lower.size() >= 3 && lower[2] == L'\\')
                return lower.substr(2); // includes the leading separator
            return lower.substr(2);
        }
        if (lower.rfind(L"\\\\", 0) == 0)
        {
            // UNC: \\server\share\rest
            size_t second = lower.find(L'\\', 2);
            if (second == std::wstring::npos) return L"";
            size_t third = lower.find(L'\\', second + 1);
            if (third == std::wstring::npos) return L"";
            return lower.substr(third);
        }
        return lower;
    }

    static std::wstring ToLower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    static bool IsPrefixOrEqual(const std::wstring& path, const std::wstring& prefix) noexcept
    {
        if (path.size() < prefix.size()) return false;
        if (path.compare(0, prefix.size(), prefix) != 0) return false;
        if (path.size() == prefix.size()) return true;
        return path[prefix.size()] == L'\\';
    }

    // Drive-agnostic: matches X:\Users\<name>
    static bool IsUserProfileRoot(const std::wstring& lower) noexcept
    {
        // Find drive or UNC root then check \users\ segment
        size_t usersPos = lower.find(L"\\users\\");
        if (usersPos == std::wstring::npos) return false;
        std::wstring rest = lower.substr(usersPos + 7); // after the user segment
        return rest.find(L'\\') == std::wstring::npos && !rest.empty();
    }

    static bool IsInsideUserDataRoot(const std::wstring& lower) noexcept
    {
        size_t usersPos = lower.find(L"\\users\\");
        if (usersPos == std::wstring::npos) return false;
        size_t nameEnd = lower.find(L'\\', usersPos + 7);
        if (nameEnd == std::wstring::npos) return false;
        std::wstring suffix = lower.substr(nameEnd);
        static const std::wstring kRoots[] = { L"\\documents", L"\\pictures", L"\\desktop", L"\\downloads" };
        for (auto const& r : kRoots)
        {
            if (suffix == r || (suffix.size() > r.size() && suffix.compare(0, r.size(), r) == 0 && suffix[r.size()] == L'\\'))
                return true;
        }
        return false;
    }
};

} // namespace Orbit::Core
