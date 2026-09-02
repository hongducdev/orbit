#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <cwctype>
#include "CleanCategoryRegistry.h"
#include <winrt/Windows.Storage.h>


namespace Orbit::Core
{

struct WhitelistEntry
{
    std::wstring pattern; // glob or absolute prefix; stored as user typed
    std::optional<CleanCategoryId> category; // nullopt => global
};

// WhitelistStore — glob + absolute-prefix matching, persisted to %LOCALAPPDATA%\Orbit\whitelist.json
// Thread-safe for read (IsWhitelisted) after Load.
class WhitelistStore
{
public:
    WhitelistStore() { Load(); }

    bool Add(std::wstring pattern, std::optional<CleanCategoryId> cat = std::nullopt)
    {
        std::replace(pattern.begin(), pattern.end(), L'/', L'\\');
        pattern = StripLongPrefix(pattern);
        while (!pattern.empty() && iswspace(pattern.front())) pattern.erase(pattern.begin());
        while (!pattern.empty() && iswspace(pattern.back())) pattern.pop_back();
        if (pattern.empty()) return false;

        std::lock_guard<std::mutex> lock(m_mutex);
        auto duplicate = std::find_if(
            m_entries.begin(),
            m_entries.end(),
            [&](auto const& entry) {
                return entry.category == cat &&
                    _wcsicmp(entry.pattern.c_str(), pattern.c_str()) == 0;
            });
        if (duplicate != m_entries.end()) return true;
        m_entries.push_back({ std::move(pattern), cat });
        if (SaveLocked()) return true;
        m_entries.pop_back();
        return false;
    }

    bool Remove(const std::wstring& pattern)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find_if(m_entries.begin(), m_entries.end(),
            [&](auto const& e) { return _wcsicmp(e.pattern.c_str(), pattern.c_str()) == 0; });
        if (it == m_entries.end()) return false;
        m_entries.erase(it);
        SaveLocked();
        return true;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
        SaveLocked();
    }

    std::vector<WhitelistEntry> Entries() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries;
    }

    // Glob + prefix matching. Case-insensitive.
    bool IsWhitelisted(std::wstring_view rawPath, CleanCategoryId cat) const
    {
        std::wstring path(rawPath);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        // Handle \\?\ prefix like ProtectionList
        path = StripLongPrefix(path);
        std::wstring lowerPath = ToLower(path);

        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& e : m_entries)
        {
            if (e.category.has_value() && e.category.value() != cat) continue;
            if (Matches(lowerPath, ToLower(e.pattern)))
                return true;
        }
        return false;
    }

    bool IsWhitelisted(std::wstring_view rawPath) const
    {
        std::wstring path(rawPath);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        path = StripLongPrefix(path);
        std::wstring lowerPath = ToLower(path);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& e : m_entries)
        {
            if (Matches(lowerPath, ToLower(e.pattern)))
                return true;
        }
        return false;
    }

    static std::filesystem::path FilePath() noexcept
    {
        PWSTR psz = nullptr;
        std::filesystem::path base;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &psz)) && psz)
        {
            base = psz;
            ::CoTaskMemFree(psz);
        }
        else
        {
            base = std::filesystem::temp_directory_path();
        }
        base /= L"Orbit";
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        return base / L"whitelist.json";
    }

    void Load()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
        auto path = FilePath();
        std::ifstream in(path);
        std::string content;
        if (in)
        {
            content.assign(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
        }
        else
        {
            try
            {
                auto values = winrt::Windows::Storage::ApplicationData::Current()
                    .LocalSettings().Values();
                auto stored = values.TryLookup(L"Orbit.Clean.WhitelistJson");
                if (!stored) return;
                content = winrt::to_string(
                    winrt::unbox_value<winrt::hstring>(stored));
            }
            catch (...)
            {
                return;
            }
        }
        // Fallback: also support plain text one pattern per line (legacy)
        if (content.find('"') == std::string::npos)
        {
            // Plain text
            std::istringstream iss(content);
            std::string line;
            while (std::getline(iss, line))
            {
                if (line.empty()) continue;
                // trim
                auto s = Trim(line);
                if (s.empty() || s[0] == '#') continue;
                m_entries.push_back({ ToWide(s), std::nullopt });
            }
            return;
        }
        // Extract pattern strings via naive scan for "pattern":"value"
        size_t pos = 0;
        while (true)
        {
            size_t k = content.find("\"pattern\"", pos);
            if (k == std::string::npos) break;
            size_t colon = content.find(':', k);
            if (colon == std::string::npos) break;
            size_t q1 = content.find('"', colon + 1);
            if (q1 == std::string::npos) break;
            size_t q2 = content.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string pat = content.substr(q1 + 1, q2 - q1 - 1);
            // Unescape minimal
            std::string unescaped;
            unescaped.reserve(pat.size());
            for (size_t i = 0; i < pat.size(); ++i)
            {
                if (pat[i] == '\\' && i + 1 < pat.size())
                {
                    char nxt = pat[i + 1];
                    if (nxt == '"' || nxt == '\\' || nxt == '/') { unescaped += nxt; ++i; }
                    else if (nxt == 'n') { unescaped += '\n'; ++i; }
                    else unescaped += pat[i];
                }
                else unescaped += pat[i];
            }
            // category
            std::optional<CleanCategoryId> cat;
            size_t ck = content.find("\"category\"", q2);
            size_t nextPat = content.find("\"pattern\"", q2 + 1);
            if (ck != std::string::npos && (nextPat == std::string::npos || ck < nextPat))
            {
                size_t ccol = content.find(':', ck);
                if (ccol != std::string::npos)
                {
                    size_t vstart = content.find_first_not_of(" \t\r\n", ccol + 1);
                    if (vstart != std::string::npos && content.compare(vstart, 4, "null") != 0)
                    {
                        size_t vend = content.find_first_of(",}\r\n", vstart);
                        std::string num = content.substr(vstart, vend - vstart);
                        try
                        {
                            int value = std::stoi(num);
                            if (value >= 0 && value < static_cast<int>(CleanCategoryId::Count))
                            {
                                cat = static_cast<CleanCategoryId>(value);
                            }
                        }
                        catch (...) {}
                    }
                }
            }
            m_entries.push_back({ ToWide(unescaped), cat });
            pos = q2 + 1;
        }
    }

private:
    mutable std::mutex m_mutex;
    std::vector<WhitelistEntry> m_entries;

    bool SaveLocked() const
    {
        std::ostringstream json;
        json << "[\n";
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            auto const& e = m_entries[i];
            std::string pat = ToNarrow(e.pattern);
            std::string esc;
            esc.reserve(pat.size() + 8);
            for (char c : pat)
            {
                if (c == '"') esc += "\\\"";
                else if (c == '\\') esc += "\\\\";
                else if (c == '\n') esc += "\\n";
                else if (c == '\r') esc += "\\r";
                else esc += c;
            }
            json << "  {\"pattern\":\"" << esc << "\",\"category\":";
            if (e.category.has_value()) json << static_cast<int>(e.category.value());
            else json << "null";
            json << "}";
            if (i + 1 < m_entries.size()) json << ",";
            json << "\n";
        }
        json << "]\n";
        std::string content = json.str();

        auto path = FilePath();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out.good()) return false;

        try
        {
            auto values = winrt::Windows::Storage::ApplicationData::Current()
                .LocalSettings().Values();
            values.Insert(
                L"Orbit.Clean.WhitelistJson",
                winrt::box_value(winrt::to_hstring(content)));
        }
        catch (...)
        {
            // JSON remains the portable source of truth for unpackaged runs.
        }
        return true;
    }

    static bool Matches(const std::wstring& lowerPath, const std::wstring& lowerPattern)
    {
        if (lowerPattern.empty()) return false;
        // A filename-only glob applies to each path's final component.
        if (lowerPattern.find(L'*') != std::wstring::npos ||
            lowerPattern.find(L'?') != std::wstring::npos)
        {
            if (lowerPattern.find(L'\\') == std::wstring::npos)
            {
                size_t separator = lowerPath.find_last_of(L'\\');
                std::wstring_view filename = separator == std::wstring::npos
                    ? std::wstring_view(lowerPath)
                    : std::wstring_view(lowerPath).substr(separator + 1);
                return GlobMatch(std::wstring(filename), lowerPattern);
            }
            return GlobMatch(lowerPath, lowerPattern);
        }
        // Otherwise prefix match (absolute path prefix)
        if (lowerPath.size() < lowerPattern.size()) return false;
        if (lowerPath.compare(0, lowerPattern.size(), lowerPattern) != 0) return false;
        // Ensure prefix boundary: exact or next char is '\'
        if (lowerPath.size() == lowerPattern.size()) return true;
        return lowerPath[lowerPattern.size()] == L'\\';
    }

    static bool GlobMatch(const std::wstring& str, const std::wstring& pat)
    {
        // Iterative glob_match with support for * (single segment) and ** (cross segment)
        // Convert ** to sentinel then handle.
        // Simplest: DP with two pointers + backtracking for *
        // Preprocess: collapse "***" to "**", but keep ** distinct
        // We'll treat "**" as ".*" (any chars incl \), "*" as "[^\\]*"
        // Implement via recursion over tokens.
        // Tokenize pattern by '/' vs '*'
        // Easier: convert pattern to regex-like manual matching.
        // Use standard glob algorithm where * = any chars except '\', ** = any chars
        // We scan pattern char by char.

        // Normalize: replace "**" with single char 0x1 (DOUBLE_STAR)
        std::wstring normPat;
        normPat.reserve(pat.size());
        for (size_t i = 0; i < pat.size(); )
        {
            if (i + 1 < pat.size() && pat[i] == L'*' && pat[i + 1] == L'*')
            {
                normPat.push_back(L'\x01'); // sentinel for **
                // skip both * and optional following '\' or '/' will be handled as separator
                i += 2;
                // If next is slash/backslash, keep it? For "**/" we want ** to consume up to slash.
                // Keep slash as part of pattern so **/ matches zero or more dirs.
                // But our sentinel already means any chars; we'll let it handle slash.
                // If pattern had "**\", we consumed **, leave \ to be matched literally after.
                // However "**\" with sentinel + "\" will require matching "\".
                // That's okay because ** should optionally match slash.
                // So we do NOT consume slash; just continue.
            }
            else
            {
                normPat.push_back(pat[i++]);
            }
        }

        size_t si = 0, pi = 0;
        size_t starPos = std::wstring::npos, sTmp = 0;
        size_t dblStarPos = std::wstring::npos, sTmp2 = 0;

        while (si < str.size())
        {
            if (pi < normPat.size() && normPat[pi] == L'\x01')
            {
                // ** : match any chars
                dblStarPos = pi++;
                sTmp2 = si;
                // Try to match zero chars first
                continue;
            }
            else if (pi < normPat.size() && normPat[pi] == L'*')
            {
                starPos = pi++;
                sTmp = si;
                continue;
            }
            else if (pi < normPat.size() && normPat[pi] == L'?')
            {
                if (str[si] == L'\\') // ? should not match separator
                {
                    // backtrack
                }
                else
                {
                    ++pi; ++si; continue;
                }
            }
            else if (pi < normPat.size() && normPat[pi] == str[si])
            {
                ++pi; ++si; continue;
            }

            // Mismatch: backtrack to last * or **
            if (starPos != std::wstring::npos)
            {
                // * cannot cross '\'
                if (str[sTmp] == L'\\')
                {
                    // * cannot consume separator, fail this star
                    starPos = std::wstring::npos;
                    if (dblStarPos != std::wstring::npos)
                    {
                        pi = dblStarPos + 1;
                        si = ++sTmp2;
                        continue;
                    }
                    return false;
                }
                pi = starPos + 1;
                si = ++sTmp;
                continue;
            }
            if (dblStarPos != std::wstring::npos)
            {
                pi = dblStarPos + 1;
                si = ++sTmp2;
                continue;
            }
            return false;
        }
        // Consume trailing * / **
        while (pi < normPat.size() && (normPat[pi] == L'*' || normPat[pi] == L'\x01')) ++pi;
        return pi == normPat.size();
    }

    static std::wstring ToLower(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    static std::wstring StripLongPrefix(std::wstring s)
    {
        const std::wstring longPrefix = L"\\\\?\\";
        const std::wstring uncPrefix = L"\\\\?\\UNC\\";
        if (s.size() >= uncPrefix.size() && s.compare(0, uncPrefix.size(), uncPrefix) == 0)
            s = L"\\\\" + s.substr(uncPrefix.size());
        else if (s.size() >= longPrefix.size() && s.compare(0, longPrefix.size(), longPrefix) == 0)
            s = s.substr(longPrefix.size());
        const std::wstring devPrefix = L"\\\\.\\";
        if (s.size() >= devPrefix.size() && s.compare(0, devPrefix.size(), devPrefix) == 0)
            s = s.substr(devPrefix.size());
        return s;
    }

    static std::string ToNarrow(const std::wstring& value)
    {
        if (value.empty()) return {};
        int needed = ::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return {};
        std::string output(static_cast<size_t>(needed), '\0');
        if (::WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), output.data(), needed,
                nullptr, nullptr) != needed)
        {
            return {};
        }
        return output;
    }

    static std::wstring ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        int needed = ::MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0);
        if (needed <= 0) return {};
        std::wstring output(static_cast<size_t>(needed), L'\0');
        if (::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), output.data(), needed) != needed)
        {
            return {};
        }
        return output;
    }

    static std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
};

} // namespace Orbit::Core
