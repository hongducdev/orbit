#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdint>
#include <wil/com.h>
#include <wil/resource.h>

namespace Orbit::Platform
{

// ShellOperations — IFileOperation wrapper for Recycle Bin moves.
// Must be called from STA (UI thread) or we create our own STA via CoInitializeEx.
// Uses FOF_ALLOWUNDO so Explorer Restore works.
class ShellOperations
{
public:
    struct DeleteResult
    {
        bool succeeded{ false };
        HRESULT hr{ S_OK };
        size_t requestedCount{ 0 };
        size_t completedCount{ 0 };
        std::wstring error;
        std::vector<std::wstring> completedPaths;
        std::vector<std::wstring> failedPaths;

        bool IsPartial() const noexcept
        {
            return completedCount > 0 && completedCount < requestedCount;
        }
    };

    static DeleteResult DeleteFiles(
        const std::vector<std::wstring>& paths,
        bool permanent)
    {
        if (paths.empty()) return { true, S_OK, 0, 0, L"" };

        HRESULT initializeResult =
            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (initializeResult == RPC_E_CHANGED_MODE)
        {
            return {
                false,
                initializeResult,
                paths.size(),
                0,
                L"File deletion requires a single-threaded COM apartment"
            };
        }
        if (FAILED(initializeResult))
        {
            return {
                false,
                initializeResult,
                paths.size(),
                0,
                L"COM initialization failed"
            };
        }

        wil::com_ptr<IFileOperation> operation;
        HRESULT result = ::CoCreateInstance(
            CLSID_FileOperation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&operation));
        if (FAILED(result))
        {
            ::CoUninitialize();
            return {
                false,
                result,
                paths.size(),
                0,
                L"Windows file operation service is unavailable"
            };
        }

        DWORD flags =
            FOF_NOCONFIRMATION |
            FOFX_SHOWELEVATIONPROMPT |
            FOF_NOERRORUI |
            FOF_SILENT;
        if (!permanent)
        {
            flags |= FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE;
        }
        result = operation->SetOperationFlags(flags);
        if (FAILED(result))
        {
            ::CoUninitialize();
            return {
                false,
                result,
                paths.size(),
                0,
                L"Could not configure the Windows file operation"
            };
        }

        std::vector<std::wstring> queuedPaths;
        std::vector<std::wstring> failedPaths;
        queuedPaths.reserve(paths.size());
        failedPaths.reserve(paths.size());
        HRESULT firstQueueError = S_OK;
        for (auto const& path : paths)
        {
            wil::com_ptr<IShellItem> item;
            std::wstring shellPath = EnsureLongPath(path);
            HRESULT itemResult = ::SHCreateItemFromParsingName(
                shellPath.c_str(),
                nullptr,
                IID_PPV_ARGS(&item));
            if (SUCCEEDED(itemResult))
            {
                itemResult = operation->DeleteItem(item.get(), nullptr);
            }
            if (SUCCEEDED(itemResult))
            {
                queuedPaths.push_back(path);
            }
            else
            {
                failedPaths.push_back(path);
                if (SUCCEEDED(firstQueueError))
                {
                    firstQueueError = itemResult;
                }
            }
        }

        if (queuedPaths.empty())
        {
            ::CoUninitialize();
            DeleteResult noItems{
                false,
                FAILED(firstQueueError) ? firstQueueError : E_FAIL,
                paths.size(),
                0,
                L"No selected paths could be queued for deletion"
            };
            noItems.failedPaths = std::move(failedPaths);
            return noItems;
        }

        result = operation->PerformOperations();
        BOOL aborted = FALSE;
        operation->GetAnyOperationsAborted(&aborted);

        std::vector<std::wstring> completedPaths;
        completedPaths.reserve(queuedPaths.size());
        for (auto const& path : queuedPaths)
        {
            DWORD attributes = ::GetFileAttributesW(EnsureLongPath(path).c_str());
            DWORD pathError = attributes == INVALID_FILE_ATTRIBUTES
                ? ::GetLastError()
                : ERROR_SUCCESS;
            if (attributes == INVALID_FILE_ATTRIBUTES &&
                (pathError == ERROR_FILE_NOT_FOUND ||
                 pathError == ERROR_PATH_NOT_FOUND))
            {
                completedPaths.push_back(path);
            }
            else
            {
                failedPaths.push_back(path);
            }
        }
        ::CoUninitialize();

        if (completedPaths.size() == paths.size())
        {
            DeleteResult complete{ true, S_OK, paths.size(), paths.size(), L"" };
            complete.completedPaths = std::move(completedPaths);
            return complete;
        }

        HRESULT failure = FAILED(result)
            ? result
            : (aborted
                ? HRESULT_FROM_WIN32(ERROR_CANCELLED)
                : (FAILED(firstQueueError) ? firstQueueError : E_FAIL));
        bool accessDenied =
            failure == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) ||
            failure == E_ACCESSDENIED;
        DeleteResult partial{
            false,
            failure,
            paths.size(),
            completedPaths.size(),
            accessDenied
                ? L"Access denied — run Orbit as administrator for protected caches"
                : (aborted
                    ? L"The file operation was cancelled; remaining files stay selected"
                    : L"Some selected files could not be deleted; remaining files stay selected")
        };
        partial.completedPaths = std::move(completedPaths);
        partial.failedPaths = std::move(failedPaths);
        return partial;
    }

    static uint64_t GetRecycleBinSizeBytes()
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof(info);
        return SUCCEEDED(::SHQueryRecycleBinW(nullptr, &info))
            ? static_cast<uint64_t>(info.i64Size)
            : 0;
    }

    static uint32_t GetRecycleBinItemCount()
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof(info);
        return SUCCEEDED(::SHQueryRecycleBinW(nullptr, &info))
            ? static_cast<uint32_t>(info.i64NumItems)
            : 0;
    }
    static DeleteResult EmptyRecycleBin()
    {
        constexpr wchar_t summaryPath[] = L"Recycle Bin (all drives)";
        HRESULT result = ::SHEmptyRecycleBinW(
            nullptr,
            nullptr,
            SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        DeleteResult outcome{
            SUCCEEDED(result),
            result,
            1,
            SUCCEEDED(result) ? 1u : 0u,
            SUCCEEDED(result) ? L"" : L"The Recycle Bin could not be emptied"
        };
        if (outcome.succeeded)
        {
            outcome.completedPaths.emplace_back(summaryPath);
        }
        else
        {
            outcome.failedPaths.emplace_back(summaryPath);
        }
        return outcome;
    }


    static std::wstring FormatBytes(uint64_t bytes) noexcept
    {
        const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            ++unit;
        }
        wchar_t buffer[64]{};
        if (unit == 0)
        {
            swprintf_s(
                buffer,
                L"%llu %s",
                static_cast<unsigned long long>(bytes),
                units[unit]);
        }
        else
        {
            swprintf_s(buffer, L"%.1f %s", value, units[unit]);
        }
        return buffer;
    }

private:
    static std::wstring EnsureLongPath(const std::wstring& path)
    {
        if (path.rfind(L"\\\\?\\", 0) == 0 ||
            path.rfind(L"\\\\.\\", 0) == 0)
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

} // namespace Orbit::Platform
