#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
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

    // Progress callback for delete operations
    using ProgressCallback = std::function<void(uint32_t completed, uint32_t total)>;

private:
    // RAII guard for COM initialization — uninitializes only if we initialized (S_OK)
    class ComInitializer
    {
    public:
        ComInitializer() noexcept
        {
            m_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            m_needsUninit = (m_hr == S_OK);
        }

        ~ComInitializer() noexcept
        {
            if (m_needsUninit)
            {
                ::CoUninitialize();
            }
        }

        ComInitializer(const ComInitializer&) = delete;
        ComInitializer& operator=(const ComInitializer&) = delete;

        HRESULT Result() const noexcept { return m_hr; }
        bool Succeeded() const noexcept { return SUCCEEDED(m_hr); }

    private:
        HRESULT m_hr{ E_FAIL };
        bool m_needsUninit{ false };
    };

    // IFileOperationProgressSink implementation for progress tracking
    class FileOperationProgressSink : public IFileOperationProgressSink
    {
    public:
        FileOperationProgressSink(
            uint32_t totalItems,
            ProgressCallback callback) :
            m_refCount(1),
            m_totalItems(totalItems),
            m_completedItems(0),
            m_callback(std::move(callback))
        {
        }

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
        {
            if (!ppv) return E_POINTER;
            *ppv = nullptr;
            if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink)
            {
                *ppv = static_cast<IFileOperationProgressSink*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override
        {
            return InterlockedIncrement(&m_refCount);
        }

        STDMETHODIMP_(ULONG) Release() override
        {
            ULONG count = InterlockedDecrement(&m_refCount);
            if (count == 0)
            {
                delete this;
            }
            return count;
        }

        // IFileOperationProgressSink
        STDMETHODIMP StartOperations() override { return S_OK; }
        STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
        STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
        STDMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
        STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
        STDMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
        STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
        STDMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
        STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }

        STDMETHODIMP PostDeleteItem(
            DWORD,
            IShellItem*,
            HRESULT hrDelete,
            IShellItem*) override
        {
            if (SUCCEEDED(hrDelete))
            {
                ++m_completedItems;
                if (m_callback)
                {
                    m_callback(m_completedItems, m_totalItems);
                }
            }
            return S_OK;
        }

        STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
        STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
        STDMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
        STDMETHODIMP ResetTimer() override { return S_OK; }
        STDMETHODIMP PauseTimer() override { return S_OK; }
        STDMETHODIMP ResumeTimer() override { return S_OK; }

    private:
        volatile ULONG m_refCount;
        uint32_t m_totalItems;
        std::atomic<uint32_t> m_completedItems;
        ProgressCallback m_callback;
    };

public:

    static DeleteResult DeleteFiles(
        const std::vector<std::wstring>& paths,
        bool permanent,
        ProgressCallback progressCallback = nullptr)
    {
        if (paths.empty()) return { true, S_OK, 0, 0, L"" };

        ComInitializer comInit;
        HRESULT initializeResult = comInit.Result();
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
        if (!comInit.Succeeded())
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

        // Attach progress sink if callback provided
        DWORD progressCookie = 0;
        FileOperationProgressSink* progressSink = nullptr;
        if (progressCallback)
        {
            progressSink = new FileOperationProgressSink(
                static_cast<uint32_t>(queuedPaths.size()),
                progressCallback);
            result = operation->Advise(progressSink, &progressCookie);
            if (FAILED(result))
            {
                progressSink->Release();
                progressSink = nullptr;
            }
        }

        result = operation->PerformOperations();

        if (progressSink)
        {
            operation->Unadvise(progressCookie);
            progressSink->Release();
        }

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
