#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <initializer_list>

namespace Orbit::Core
{

enum class FileCategoryHint : uint8_t
{
    Unknown = 0,
    Media,
    Docs,
    System,
    Large
};

inline FileCategoryHint InferFileCategoryHint(
    std::wstring_view path,
    uint64_t size) noexcept
{
    std::wstring lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    if (lower.find(L"\\windows") != std::wstring::npos ||
        lower.find(L"\\program files") != std::wstring::npos ||
        lower.find(L"\\programdata") != std::wstring::npos)
    {
        return FileCategoryHint::System;
    }
    if (size > (1ull << 30))
    {
        return FileCategoryHint::Large;
    }

    size_t dot = lower.find_last_of(L'.');
    std::wstring_view ext = (dot == std::wstring::npos)
        ? std::wstring_view{}
        : std::wstring_view(lower).substr(dot);
    auto isExt = [&](std::initializer_list<std::wstring_view> list) {
        for (auto candidate : list)
        {
            if (ext == candidate) return true;
        }
        return false;
    };
    if (isExt({ L".jpg", L".jpeg", L".png", L".gif", L".webp", L".heic", L".mp4",
                L".mkv", L".mov", L".avi", L".webm", L".mp3", L".wav", L".flac", L".aac" }))
    {
        return FileCategoryHint::Media;
    }
    if (isExt({ L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx",
                L".txt", L".md", L".rtf", L".csv" }))
    {
        return FileCategoryHint::Docs;
    }
    if (isExt({ L".dll", L".sys", L".drv", L".exe" }))
    {
        return FileCategoryHint::System;
    }
    return FileCategoryHint::Unknown;
}

struct FileNode
{
    std::wstring name;
    std::wstring path;
    uint64_t size{ 0 };
    uint64_t logicalSize{ 0 };
    bool isDir{ false };
    bool isCloudPlaceholder{ false };
    bool childrenComplete{ false };
    bool scanTruncated{ false };
    FILETIME lastWrite{};
    FileCategoryHint hint{ FileCategoryHint::Unknown };
    std::vector<std::unique_ptr<FileNode>> children;
};

} // namespace Orbit::Core
