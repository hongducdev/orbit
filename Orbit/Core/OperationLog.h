#pragma once
#include <windows.h>
#include <stringapiset.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <mutex>

namespace Orbit::Core
{

enum class OperationKind : uint8_t { Clean, Uninstall, Optimize, Analyze };
enum class OperationOutcome : uint8_t { Success, Partial, Failed };
enum class Destination : uint8_t { Recycle, Permanent };

struct OperationLogEntry
{
    std::wstring timestampIso8601;
    OperationKind op{ OperationKind::Clean };
    std::wstring category;
    std::vector<std::wstring> paths;
    uint64_t bytesBefore{0};
    uint64_t bytesAfter{0};
    Destination dest{ Destination::Recycle };
    bool dryRun{false};
    OperationOutcome outcome{ OperationOutcome::Success };
    std::wstring error;

    std::wstring ToJson() const
    {
        auto escape = [](const std::wstring& s) -> std::wstring {
            std::wstring out;
            out.reserve(s.size() + 8);
            for (wchar_t c : s)
            {
                if (c == L'"') out += L"\\\"";
                else if (c == L'\\') out += L"\\\\";
                else if (c == L'\n') out += L"\\n";
                else if (c == L'\r') out += L"\\r";
                else if (c == L'\t') out += L"\\t";
                else if (c == L'\b') out += L"\\b";
                else if (c == L'\f') out += L"\\f";
                else if (c < 0x20)
                {
                    wchar_t buf[7];
                    swprintf_s(buf, L"\\u%04x", static_cast<int>(c));
                    out += buf;
                }
                else out += c;
            }
            return out;
        };
        auto kindStr = [&]() -> const wchar_t* {
            switch (op)
            {
            case OperationKind::Clean: return L"clean";
            case OperationKind::Uninstall: return L"uninstall";
            case OperationKind::Analyze: return L"analyze";
            default: return L"optimize";
            }
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

class OperationLog
{
public:
    static std::filesystem::path LogFilePath() noexcept
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
        return base / L"history.jsonl";
    }

    static bool Append(const OperationLogEntry& entry) noexcept
    {
        try
        {
            auto path = LogFilePath();
            std::wstring wjson = entry.ToJson();
            int needed = ::WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, wjson.data(),
                static_cast<int>(wjson.size()), nullptr, 0, nullptr, nullptr);
            if (needed <= 0) return false;
            std::string narrow(static_cast<size_t>(needed), '\0');
            if (::WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, wjson.data(),
                    static_cast<int>(wjson.size()), narrow.data(), needed,
                    nullptr, nullptr) != needed)
            {
                return false;
            }
            static std::mutex s_mutex;
            std::lock_guard<std::mutex> lock(s_mutex);
            std::ofstream out(path, std::ios::app | std::ios::binary);
            if (!out) return false;
            out.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
            out.put('\n');
            out.flush();
            return out.good();
        }
        catch (...)
        {
            return false;
        }
    }

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
