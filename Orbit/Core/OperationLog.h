#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <shlobj.h>
#include <winrt/Windows.Storage.h>

namespace Orbit::Core
{

enum class OperationKind : uint8_t { Clean, Uninstall, Optimize };
enum class OperationOutcome : uint8_t { Success, Partial, Failed };
enum class Destination : uint8_t { Recycle, Permanent };

struct OperationLogEntry
{
    std::wstring timestampIso8601; // e.g., 2026-09-01T09:00:00Z
    OperationKind op{ OperationKind::Clean };
    std::wstring category; // e.g., "TempUser" or "WinUpdateCache"
    std::vector<std::wstring> paths;
    uint64_t bytesBefore{0};
    uint64_t bytesAfter{0};
    Destination dest{ Destination::Recycle };
    bool dryRun{false};
    OperationOutcome outcome{ OperationOutcome::Success };
    std::wstring error; // empty on success

    // Minimal JSON serialization (no external dep). Paths etc. are escaped for quotes/backslashes.
    std::wstring ToJson() const
    {
        auto escape = [](const std::wstring& s) -> std::wstring {
            std::wstring out;
            out.reserve(s.size() + 4);
            for (wchar_t c : s)
            {
                if (c == L'"') out += L"\\\"";
                else if (c == L'\\') out += L"\\\\";
                else if (c == L'\n') out += L"\\n";
                else if (c == L'\r') out += L"\\r";
                else if (c == L'\t') out += L"\\t";
                else out += c;
            }
            return out;
        };
        auto kindStr = [&]() -> const wchar_t* {
            switch (op) { case OperationKind::Clean: return L"clean"; case OperationKind::Uninstall: return L"uninstall"; default: return L"optimize"; }
        };
        auto destStr = (dest == Destination::Recycle) ? L"recycle" : L"perm";
        auto outcomeStr = [&]() -> const wchar_t* {
            switch (outcome) { case OperationOutcome::Success: return L"success"; case OperationOutcome::Partial: return L"partial"; default: return L"failed"; }
        };
        std::wstring json = L"{";
        json += L"\"ts\":\"" + escape(timestampIso8601) + L"\",";
        json += L"\"op\":\"" + std::wstring(kindStr()) + L"\",";
        json += L"\"category\":\"" + escape(category) + L"\",";
        json += L"\"paths\":[";
        for (size_t i = 0; i < paths.size(); ++i)
        {
            if (i) json += L",";
            json += L"\"" + escape(paths[i]) + L"\"";
        }
        json += L"],";
        json += L"\"bytesBefore\":" + std::to_wstring(bytesBefore) + L",";
        json += L"\"bytesAfter\":" + std::to_wstring(bytesAfter) + L",";
        json += L"\"dest\":\"" + std::wstring(destStr) + L"\",";
        json += L"\"dryRun\":" + std::wstring(dryRun ? L"true" : L"false") + L",";
        json += L"\"outcome\":\"" + std::wstring(outcomeStr()) + L"\",";
        json += L"\"error\":";
        if (error.empty()) json += L"null";
        else json += L"\"" + escape(error) + L"\"";
        json += L"}";
        return json;
    }
};

// OperationLog — append-only JSONL file.
// Location: packaged => ApplicationData.Current.LocalFolder/Orbit/history.jsonl
//           unpackaged => %LOCALAPPDATA%\Orbit\history.jsonl
class OperationLog
{
public:
    static std::filesystem::path LogFilePath() noexcept
    {
        // Try packaged location first; fallback to LocalAppData\Orbit
        // We do not use winrt::ApplicationData here to keep header-only and avoid
        // requiring apartment init in non-UI contexts. Use SHGetKnownFolderPath.
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
        return base / L"history.jsonl";
    }

    // Append entry as single JSON line. Returns false on I/O failure.
    static bool Append(const OperationLogEntry& entry) noexcept
    {
        try
        {
            auto path = LogFilePath();
            std::wofstream out(path, std::ios::app);
            if (!out) return false;
            // Ensure UTF-8? wofstream writes UTF-16 with BOM on Windows; for JSONL we write wide then
            // consumer must handle. For mo-parity, write UTF-8 via narrow conversion would be better,
            // but keep simple for v1 header-only. Write as UTF-8 narrow via conversion.
            // Convert wstring json to narrow UTF-8 (assume ASCII paths for now; non-ASCII escaped would need proper conversion).
            std::wstring wjson = entry.ToJson();
            // Quick narrow: if all chars < 0x80, direct; else replace with ?
            std::string narrow;
            narrow.reserve(wjson.size());
            for (wchar_t c : wjson)
            {
                if (c < 0x80) narrow.push_back(static_cast<char>(c));
                else narrow.push_back('?');
            }
            // Reopen as narrow append
            out.close();
            std::ofstream nout(path, std::ios::app | std::ios::binary);
            if (!nout) return false;
            nout << narrow << "\n";
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // Helper to produce ISO8601 UTC now.
    static std::wstring NowIso8601() noexcept
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t t = system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_s(&tm, &t);
        wchar_t buf[32];
        wcsftime(buf, 32, L"%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }
};

} // namespace Orbit::Core
