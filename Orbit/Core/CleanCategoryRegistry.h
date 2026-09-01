#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace Orbit::Core
{

enum class CleanTier : uint8_t
{
    Safe = 0,   // auto-selectable, conservative
    Review = 1, // checked but requires confirmation
    Risky = 2   // unchecked, hidden behind "Show risky"
};

enum class CleanCategoryId : uint8_t
{
    TempUser = 0,
    TempSystem,
    WinUpdateCache,
    DeliveryOptimization,
    ThumbCache,
    ShaderCache,
    BrowserTemp,
    DevCaches,
    WerReports,
    Prefetch, // read-only hint, never auto-delete
    RecycleBin,
    Count
};

struct CleanCategoryMeta
{
    CleanCategoryId id{ CleanCategoryId::TempUser };
    const wchar_t* name{ L"" };         // display name
    const wchar_t* description{ L"" };  // tooltip
    CleanTier tier{ CleanTier::Safe };
    bool recyclable{ true };            // true => IFileOperation FOF_ALLOWUNDO, false => size-only hint
};

class CleanCategoryRegistry
{
public:
    static const std::vector<CleanCategoryMeta>& All() noexcept
    {
        static const std::vector<CleanCategoryMeta> kAll = {
            { CleanCategoryId::TempUser, L"User Temp", L"%LOCALAPPDATA%\\Temp / %TEMP%", CleanTier::Safe, true },
            { CleanCategoryId::TempSystem, L"System Temp", L"C:\\Windows\\Temp", CleanTier::Review, true },
            { CleanCategoryId::WinUpdateCache, L"Windows Update Cache", L"C:\\Windows\\SoftwareDistribution\\Download", CleanTier::Review, true },
            { CleanCategoryId::DeliveryOptimization, L"Delivery Optimization", L"DeliveryOptimization\\Cache", CleanTier::Safe, true },
            { CleanCategoryId::ThumbCache, L"Thumbnail Cache", L"Explorer thumbcache_*.db", CleanTier::Safe, true },
            { CleanCategoryId::ShaderCache, L"DirectX Shader Cache", L"D3DSCache / ShaderCache", CleanTier::Safe, true },
            { CleanCategoryId::BrowserTemp, L"Browser Temp", L"Edge / Chrome Cache & Code Cache", CleanTier::Review, true },
            { CleanCategoryId::DevCaches, L"Dev Caches", L"npm / Yarn / pnpm / NuGet / VS / pip", CleanTier::Review, true },
            { CleanCategoryId::WerReports, L"Windows Error Reports", L"WER ReportQueue / ReportArchive", CleanTier::Safe, true },
            { CleanCategoryId::Prefetch, L"Prefetch (hint)", L"C:\\Windows\\Prefetch — size only, no delete", CleanTier::Risky, false },
            { CleanCategoryId::RecycleBin, L"Recycle Bin", L"Per-drive Recycle Bin size", CleanTier::Review, true },
        };
        return kAll;
    }

    static const CleanCategoryMeta* Find(CleanCategoryId id) noexcept
    {
        for (auto const& m : All())
            if (m.id == id) return &m;
        return nullptr;
    }

    static bool IsRecyclable(CleanCategoryId id) noexcept
    {
        auto* m = Find(id);
        return m ? m->recyclable : false;
    }

    static CleanTier Tier(CleanCategoryId id) noexcept
    {
        auto* m = Find(id);
        return m ? m->tier : CleanTier::Risky;
    }
};

} // namespace Orbit::Core
