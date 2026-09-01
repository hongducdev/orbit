#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include "CleanCategoryRegistry.h"

namespace Orbit::Core
{

// Resolves filesystem roots for each category. Returns existing paths only.
// Missing categories yield empty vector with reason "Not present on this system".
class CleanCategoryProber
{
public:
    static std::vector<std::wstring> Probe(CleanCategoryId id)
    {
        std::vector<std::wstring> roots;
        auto folder = [](REFKNOWNFOLDERID rfid) -> std::wstring {
            PWSTR psz = nullptr;
            if (SUCCEEDED(::SHGetKnownFolderPath(rfid, 0, nullptr, &psz)) && psz)
            {
                std::wstring s = psz;
                ::CoTaskMemFree(psz);
                return s;
            }
            return L"";
        };

        auto exists = [](const std::wstring& p) -> bool {
            DWORD a = ::GetFileAttributesW(p.c_str());
            return a != INVALID_FILE_ATTRIBUTES;
        };

        auto addIfExists = [&](std::wstring p) {
            while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
            {
                p.pop_back();
            }
            if (p.empty() || !exists(p)) return;
            for (auto const& root : roots)
            {
                if (_wcsicmp(root.c_str(), p.c_str()) == 0) return;
            }
            roots.push_back(std::move(p));
        };

        auto localAppData = folder(FOLDERID_LocalAppData);
        auto roamingAppData = folder(FOLDERID_RoamingAppData);
        auto programData = folder(FOLDERID_ProgramData);
        auto tempFromEnv = []() -> std::wstring {
            wchar_t buf[MAX_PATH * 2]{};
            DWORD n = ::GetEnvironmentVariableW(L"TEMP", buf, MAX_PATH * 2);
            if (n > 0 && n < MAX_PATH * 2) return buf;
            n = ::GetEnvironmentVariableW(L"TMP", buf, MAX_PATH * 2);
            if (n > 0 && n < MAX_PATH * 2) return buf;
            return L"";
        }();

        switch (id)
        {
        case CleanCategoryId::TempUser: {
            if (!tempFromEnv.empty()) addIfExists(tempFromEnv);
            // Also LOCALAPPDATA\Temp
            if (!localAppData.empty()) addIfExists(localAppData + L"\\Temp");
            break;
        }
        case CleanCategoryId::TempSystem: {
            wchar_t winDir[MAX_PATH]{};
            ::GetWindowsDirectoryW(winDir, MAX_PATH);
            std::wstring p = std::wstring(winDir) + L"\\Temp";
            addIfExists(p);
            break;
        }
        case CleanCategoryId::WinUpdateCache: {
            wchar_t winDir[MAX_PATH]{};
            ::GetWindowsDirectoryW(winDir, MAX_PATH);
            addIfExists(std::wstring(winDir) + L"\\SoftwareDistribution\\Download");
            break;
        }
        case CleanCategoryId::DeliveryOptimization: {
            // Delivery Optimization cache varies; common locations
            if (!localAppData.empty()) addIfExists(localAppData + L"\\Microsoft\\Windows\\DeliveryOptimization\\Cache");
            // Also ProgramData flavor
            if (!programData.empty()) addIfExists(programData + L"\\Microsoft\\Windows\\DeliveryOptimization\\Cache");
            wchar_t winDir[MAX_PATH]{};
            if (::GetWindowsDirectoryW(winDir, MAX_PATH) > 0)
            {
                addIfExists(std::wstring(winDir) + L"\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache");
            }
            break;
        }
        case CleanCategoryId::ThumbCache: {
            if (!localAppData.empty()) addIfExists(localAppData + L"\\Microsoft\\Windows\\Explorer");
            break;
        }
        case CleanCategoryId::ShaderCache: {
            if (!localAppData.empty())
            {
                addIfExists(localAppData + L"\\D3DSCache");
                addIfExists(localAppData + L"\\Microsoft\\DirectX\\ShaderCache");
            }
            // Intel shader cache etc.
            break;
        }
        case CleanCategoryId::BrowserTemp: {
            if (!localAppData.empty())
            {
                addIfExists(localAppData + L"\\Microsoft\\Edge\\User Data\\Default\\Cache");
                addIfExists(localAppData + L"\\Microsoft\\Edge\\User Data\\Default\\Code Cache");
                addIfExists(localAppData + L"\\Google\\Chrome\\User Data\\Default\\Cache");
                addIfExists(localAppData + L"\\Google\\Chrome\\User Data\\Default\\Code Cache");
                // EBWebView (WebView2)
                addIfExists(localAppData + L"\\Microsoft\\Edge\\EBWebView\\Default\\Cache");
            }
            break;
        }
        case CleanCategoryId::DevCaches: {
            // User profile based
            std::wstring userProfile;
            {
                wchar_t buf[MAX_PATH * 2]{};
                DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH * 2);
                if (n > 0 && n < MAX_PATH * 2) userProfile = buf;
            }
            if (!userProfile.empty())
            {
                addIfExists(userProfile + L"\\.npm\\_cacache");
                addIfExists(userProfile + L"\\.yarn\\cache");
                addIfExists(userProfile + L"\\.pnpm-store");
                addIfExists(userProfile + L"\\.cache\\pip");
                addIfExists(userProfile + L"\\AppData\\Local\\pip\\Cache");
                // Also NuGet http-cache
                if (!localAppData.empty()) addIfExists(localAppData + L"\\NuGet\\v3-cache");
                addIfExists(userProfile + L"\\.nuget\\v3-cache");
            }
            if (!localAppData.empty())
            {
                addIfExists(localAppData + L"\\npm-cache");
                addIfExists(localAppData + L"\\Yarn\\Cache");
                addIfExists(localAppData + L"\\pnpm\\store");
            }
            break;
        }
        case CleanCategoryId::WerReports: {
            if (!programData.empty())
            {
                addIfExists(programData + L"\\Microsoft\\Windows\\WER\\ReportQueue");
                addIfExists(programData + L"\\Microsoft\\Windows\\WER\\ReportArchive");
            }
            if (!localAppData.empty()) addIfExists(localAppData + L"\\Microsoft\\Windows\\WER\\ReportQueue");
            break;
        }
        case CleanCategoryId::Prefetch: {
            wchar_t winDir[MAX_PATH]{};
            ::GetWindowsDirectoryW(winDir, MAX_PATH);
            addIfExists(std::wstring(winDir) + L"\\Prefetch");
            break;
        }
        case CleanCategoryId::RecycleBin: {
            // Recycle bin is virtual; return special marker — caller should use SHQueryRecycleBin
            // Return empty to indicate size-only via ShellOperations
            break;
        }
        default: break;
        }
        return roots;
    }

    // For categories with file-pattern semantics (e.g., ThumbCache thumbcache_*.db),
    // returns true and fills pattern. Caller filters enumeration by pattern.
    static bool FilePattern(CleanCategoryId id, std::wstring& outPattern) noexcept
    {
        if (id == CleanCategoryId::ThumbCache) { outPattern = L"thumbcache_*.db"; return true; }
        return false;
    }
};

} // namespace Orbit::Core
