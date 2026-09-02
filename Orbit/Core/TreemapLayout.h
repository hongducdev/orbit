#pragma once
#include "FileNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Orbit::Core
{

struct TreemapRect
{
    double x{ 0 };
    double y{ 0 };
    double width{ 0 };
    double height{ 0 };

    double Right() const noexcept { return x + width; }
    double Bottom() const noexcept { return y + height; }
    double Area() const noexcept { return width * height; }
};

struct TreemapItem
{
    TreemapRect bounds;
    FileNode const* node{ nullptr };
    bool isGroupPlaceholder{ false };
    size_t groupedCount{ 0 };
    uint64_t groupedSize{ 0 };
};

struct TreemapOptions
{
    size_t maxRects{ 500 };
    uint64_t groupSmallBytes{ 0 };
    bool showFiles{ true };
    bool showFolders{ true };
    double minPixel{ 2.0 };
    std::wstring nameFilterLower;
    bool sortByName{ false };
};

class TreemapLayout
{
public:
    static std::vector<TreemapItem> Layout(
        FileNode const& root,
        TreemapRect bounds,
        TreemapOptions const& options);

    static bool SelfTest() noexcept;
};

} // namespace Orbit::Core
