#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <shlwapi.h>

namespace Orbit::Core
{

// ProtectionList — denylist for paths that must never be auto-deleted.
// Mirrors Mole's conservative whitelist: Windows/system dirs, user profile
// root without subpath, reparse/cloud placeholders handled via attributes
// (not just path) in SizeCalculator. This file is path-only checks.
class ProtectionList
{
public:
    // Returns true if |path| is protected (must not delete without explicit user selection).
    // |path| may be absolute or with \\?\ prefix; comparison is case-insensitive,
    // normalized to backslashes, trailing slash trimmed except for drive root.
    static bool IsProtected(std::wstring_view path) noexcept
    {
        if (path.empty()) return true;

        std::wstring norm = Normalize(path);
        std::wstring lower = ToLower(norm);

        // Denied prefixes — subtree protected.
        // Keep list small & auditable; OneDrive/WSL/network handled via reparse checks elsewhere.
        static const std::wstring kDeniedPrefixes[] = {
            L"c:\\windows",
            L"c:\\windows\\system32",
            L"c:\\windows\\winsxs",
            L"c:\\windows\\systemapps",
            L"c:\\windows\\servicing",
            L"c:\\program files\\windowsapps",
            L"c:\\system volume information",
            L"c:\\$recycle.bin",
            L"c:\\recovery",
        };

        for (auto const& p : kDeniedPrefixes)
        {
            if (IsPrefixOrEqual(lower, p))
                return true;
        }

        // User profile root without subpath is protected (e.g., C:\Users\alice).
        // Allow subpaths like C:\Users\alice\AppData\Local\Temp\...
        // Detect pattern C:\Users\<name> with no further separator.
        if (IsUserProfileRoot(lower))
            return true;

        // Documents / Pictures / Desktop / Downloads roots — never auto-select.
        // They are protected from bulk delete; explicit per-file selection still requires UI confirm.
        static const std::wstring kUserDataRoots[] = {
            L"\\documents",
            L"\\pictures",
            L"\\desktop",
            L"\\downloads",
        };
        // Check if path equals or is inside those folders under the profile.
        // We already know profile root; expand check for any user.
        if (IsInsideUserDataRoot(lower))
            return true;

        return false;
    }

    // Lightweight check for reparse/cloud placeholder by attributes — caller passes
    // GetFileAttributesEx result. Separated so ProtectionList stays path-only testable.
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
        // Strip \\?\ prefix for comparison.
        const std::wstring prefix = L"\\\\?\\";
        if (s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0)
            s = s.substr(prefix.size());
        // Normalize slashes to backslash.
        std::replace(s.begin(), s.end(), L'/', L'\\');
        // Trim trailing backslashes except for "C:\"
        while (s.size() > 3 && (s.back() == L'\\'))
            s.pop_back();
        return s;
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

    static bool IsUserProfileRoot(const std::wstring& lower) noexcept
    {
        const std::wstring usersPrefix = L"c:\\users\\";
        if (lower.rfind(usersPrefix, 0) != 0) return false;
        std::wstring rest = lower.substr(usersPrefix.size());
        // No further backslash => exactly C:\Users\<name>
        return rest.find(L'\\') == std::wstring::npos && !rest.empty();
    }

    static bool IsInsideUserDataRoot(const std::wstring& lower) noexcept
    {
        // Find \users\<name>\ pattern, then check suffix
        const std::wstring usersPrefix = L"c:\\users\\";
        if (lower.rfind(usersPrefix, 0) != 0) return false;
        // Extract after C:\Users\<name>\
        size_t nameEnd = lower.find(L'\\', usersPrefix.size());
        if (nameEnd == std::wstring::npos) return false; // profile root itself handled above
        std::wstring suffix = lower.substr(nameEnd); // e.g., \documents or \documents\foo
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
