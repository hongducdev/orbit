#include "pch.h"
#include "FastDirSize.h"

#include "../Platform/PathHelpers.h"

#include <condition_variable>
#include <mutex>
#include <thread>

using Orbit::Platform::PathHelpers;

namespace
{
    bool SkipName(wchar_t const* name, size_t chars) noexcept
    {
        if (chars == 1 && name[0] == L'.') return true;
        if (chars == 2 && name[0] == L'.' && name[1] == L'.') return true;
        if (chars == 13 && _wcsnicmp(name, L"$Recycle.Bin", 13) == 0) return true;
        if (chars == 26 && _wcsnicmp(name, L"System Volume Information", 26) == 0) return true;
        return false;
    }

    bool SkipAttrs(DWORD attributes) noexcept
    {
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
        if (PathHelpers::IsOneDrivePlaceholder(attributes)) return true;
        return false;
    }

    bool SkipExcluded(
        wchar_t const* name,
        size_t chars,
        std::wstring const& dir,
        std::wstring const& start,
        std::wstring_view excludeImmediate) noexcept
    {
        if (excludeImmediate.empty()) return false;
        if (dir.size() != start.size() || _wcsicmp(dir.c_str(), start.c_str()) != 0) return false;
        if (chars != excludeImmediate.size()) return false;
        return _wcsnicmp(name, excludeImmediate.data(), chars) == 0;
    }

    void Accept(
        Orbit::Core::DirVisit& out,
        std::wstring const& dir,
        wchar_t const* name,
        size_t chars,
        DWORD attributes,
        uint64_t size)
    {
        if (SkipName(name, chars) || SkipAttrs(attributes)) return;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            out.subdirs.push_back(PathHelpers::Join(dir, std::wstring(name, chars)));
            return;
        }
        out.fileBytes += size;
        ++out.fileCount;
    }

    HANDLE OpenDir(std::wstring const& dir)
    {
        wchar_t const* path = dir.c_str();
        std::wstring prefixed;
        if (dir.size() >= MAX_PATH && dir.rfind(L"\\\\?\\", 0) != 0)
        {
            prefixed = PathHelpers::EnsureLongPath(dir);
            path = prefixed.c_str();
        }
        return ::CreateFileW(
            path,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
    }

    bool VisitBuffered(std::wstring const& dir, std::atomic<bool> const& cancel, Orbit::Core::DirVisit& out)
    {
        HANDLE handle = OpenDir(dir);
        if (handle == INVALID_HANDLE_VALUE) return false;

        thread_local std::vector<BYTE> buffer;
        if (buffer.size() < 256 * 1024) buffer.resize(256 * 1024);
        bool gotChunk = false;
        for (;;)
        {
            if (cancel.load(std::memory_order_relaxed)) break;
            if (!::GetFileInformationByHandleEx(
                    handle, FileFullDirectoryInfo, buffer.data(), static_cast<DWORD>(buffer.size())))
            {
                DWORD err = ::GetLastError();
                if (err == ERROR_MORE_DATA && buffer.size() < 1024 * 1024)
                {
                    buffer.resize(buffer.size() * 2);
                    continue;
                }
                ::CloseHandle(handle);
                return gotChunk || err == ERROR_NO_MORE_FILES;
            }
            gotChunk = true;
            auto* info = reinterpret_cast<FILE_FULL_DIR_INFO*>(buffer.data());
            for (;;)
            {
                size_t chars = info->FileNameLength / sizeof(WCHAR);
                uint64_t size = static_cast<uint64_t>(info->EndOfFile.QuadPart);
                Accept(out, dir, info->FileName, chars, info->FileAttributes, size);
                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<FILE_FULL_DIR_INFO*>(
                    reinterpret_cast<BYTE*>(info) + info->NextEntryOffset);
            }
        }

        ::CloseHandle(handle);
        return true;
    }

    void VisitFind(std::wstring const& dir, std::atomic<bool> const& cancel, Orbit::Core::DirVisit& out)
    {
        std::wstring pattern = PathHelpers::Join(dir, L"*");
        WIN32_FIND_DATAW data{};
        HANDLE find = ::FindFirstFileExW(
            PathHelpers::EnsureLongPath(pattern).c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH | FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY);
        if (find == INVALID_HANDLE_VALUE)
        {
            find = ::FindFirstFileExW(
                PathHelpers::EnsureLongPath(pattern).c_str(),
                FindExInfoBasic,
                &data,
                FindExSearchNameMatch,
                nullptr,
                FIND_FIRST_EX_LARGE_FETCH);
        }
        if (find == INVALID_HANDLE_VALUE) return;
        do
        {
            if (cancel.load()) break;
            size_t chars = wcsnlen(data.cFileName, MAX_PATH);
            uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            Accept(out, dir, data.cFileName, chars, data.dwFileAttributes, size);
        } while (::FindNextFileW(find, &data));
        ::FindClose(find);
    }

    unsigned WorkerCount(unsigned requested) noexcept
    {
        unsigned hardware = std::thread::hardware_concurrency();
        if (hardware == 0) hardware = 4;
        unsigned n = requested ? requested : (std::min)(12u, (std::max)(2u, hardware));
        return (std::max)(1u, n);
    }
}

namespace Orbit::Core
{
    DirVisit FastDirSize::Visit(
        std::wstring const& dir,
        std::atomic<bool> const& cancel,
        std::wstring const& startRoot,
        std::wstring_view excludeImmediate)
    {
        DirVisit raw;
        if (!VisitBuffered(dir, cancel, raw)) VisitFind(dir, cancel, raw);
        if (excludeImmediate.empty() || startRoot.empty()) return raw;

        DirVisit filtered;
        filtered.fileBytes = raw.fileBytes;
        filtered.fileCount = raw.fileCount;
        filtered.subdirs.reserve(raw.subdirs.size());
        for (auto& child : raw.subdirs)
        {
            auto slash = child.find_last_of(L"\\/");
            std::wstring name = slash == std::wstring::npos ? child : child.substr(slash + 1);
            if (SkipExcluded(name.c_str(), name.size(), dir, startRoot, excludeImmediate)) continue;
            filtered.subdirs.push_back(std::move(child));
        }
        return filtered;
    }

    uint64_t FastDirSize::Measure(
        std::wstring_view root,
        std::atomic<bool> const& cancel,
        std::atomic<uint32_t>* filesFound,
        std::atomic<uint64_t>* bytesFound,
        unsigned workers,
        std::wstring_view excludeImmediate,
        std::atomic<uint64_t>* liveSize)
    {
        std::wstring start(root);
        if (start.empty()) return 0;
        while (start.size() > 3 && (start.back() == L'\\' || start.back() == L'/')) start.pop_back();

        auto tick = [&](uint64_t total) {
            if (liveSize) liveSize->store(total, std::memory_order_relaxed);
        };

        unsigned n = WorkerCount(workers);
        if (n <= 1)
        {
            std::vector<std::wstring> stack{ start };
            uint64_t total = 0;
            uint32_t steps = 0;
            while (!stack.empty() && !cancel.load(std::memory_order_relaxed))
            {
                std::wstring dir = std::move(stack.back());
                stack.pop_back();
                auto visit = Visit(dir, cancel, start, excludeImmediate);
                total += visit.fileBytes;
                if (filesFound) filesFound->fetch_add(visit.fileCount, std::memory_order_relaxed);
                if (bytesFound) bytesFound->fetch_add(visit.fileBytes, std::memory_order_relaxed);
                for (auto& child : visit.subdirs) stack.push_back(std::move(child));
                if ((++steps & 31u) == 0) tick(total);
            }
            tick(total);
            return total;
        }

        std::mutex mu;
        std::condition_variable cv;
        std::vector<std::wstring> queue{ start };
        std::atomic<uint64_t> total{ 0 };
        int active = 0;
        bool done = false;
        auto worker = [&] {
            while (!cancel.load())
            {
                std::wstring dir;
                {
                    std::unique_lock<std::mutex> lock(mu);
                    cv.wait(lock, [&] { return !queue.empty() || done || cancel.load(); });
                    if (cancel.load() || (done && queue.empty())) return;
                    if (queue.empty()) continue;
                    dir = std::move(queue.back());
                    queue.pop_back();
                    ++active;
                }
                auto visit = Visit(dir, cancel, start, excludeImmediate);
                uint64_t now = total.fetch_add(visit.fileBytes, std::memory_order_relaxed) + visit.fileBytes;
                tick(now);
                if (filesFound) filesFound->fetch_add(visit.fileCount, std::memory_order_relaxed);
                if (bytesFound) bytesFound->fetch_add(visit.fileBytes, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(mu);
                for (auto& child : visit.subdirs) queue.push_back(std::move(child));
                --active;
                if ((queue.empty() && active == 0) || cancel.load())
                {
                    done = true;
                    cv.notify_all();
                }
                else if (!visit.subdirs.empty())
                {
                    cv.notify_all();
                }
            }
        };
        std::vector<std::thread> pool;
        pool.reserve(n);
        for (unsigned i = 0; i < n; ++i) pool.emplace_back(worker);
        for (auto& thread : pool) thread.join();
        tick(total.load());
        return total.load();
    }
}
