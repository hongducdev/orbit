#include "pch.h"
#include "TreemapLayout.h"

#include <numeric>

namespace
{
    using Orbit::Core::FileNode;
    using Orbit::Core::TreemapItem;
    using Orbit::Core::TreemapOptions;
    using Orbit::Core::TreemapRect;

    struct SizedChild
    {
        FileNode const* node{ nullptr };
        double area{ 0 };
        uint64_t bytes{ 0 };
        bool placeholder{ false };
        size_t groupedCount{ 0 };
    };

    bool Overlaps(TreemapRect const& a, TreemapRect const& b, double eps)
    {
        return a.x + eps < b.Right() && b.x + eps < a.Right() &&
            a.y + eps < b.Bottom() && b.y + eps < a.Bottom();
    }

    double WorstAspect(std::vector<double> const& row, double length)
    {
        if (row.empty() || length <= 0)
        {
            return std::numeric_limits<double>::infinity();
        }
        double sum = 0;
        double minValue = row.front();
        double maxValue = row.front();
        for (double value : row)
        {
            sum += value;
            minValue = (std::min)(minValue, value);
            maxValue = (std::max)(maxValue, value);
        }
        if (sum <= 0 || minValue <= 0)
        {
            return std::numeric_limits<double>::infinity();
        }
        double lengthSq = length * length;
        double sumSq = sum * sum;
        return (std::max)(lengthSq * maxValue / sumSq, sumSq / (lengthSq * minValue));
    }

    void LayoutRow(
        std::vector<SizedChild> const& children,
        std::vector<size_t> const& row,
        TreemapRect& remaining,
        double /*minPixel*/,
        std::vector<TreemapItem>& out)
    {
        double sum = 0;
        for (size_t index : row)
        {
            sum += children[index].area;
        }
        if (sum <= 0 || remaining.width <= 0 || remaining.height <= 0)
        {
            return;
        }

        bool horizontal = remaining.width >= remaining.height;
        double length = horizontal ? remaining.height : remaining.width;
        double thickness = sum / length;
        double maxThickness = horizontal ? remaining.width : remaining.height;
        if (thickness > maxThickness) thickness = maxThickness;

        double cursor = 0;
        for (size_t index : row)
        {
            double span = thickness > 0 ? children[index].area / thickness : 0;
            if (cursor + span > length) span = (std::max)(0.0, length - cursor);
            TreemapItem item;
            if (horizontal)
            {
                item.bounds = { remaining.x, remaining.y + cursor, thickness, span };
            }
            else
            {
                item.bounds = { remaining.x + cursor, remaining.y, span, thickness };
            }
            item.node = children[index].node;
            item.isGroupPlaceholder = children[index].placeholder;
            item.groupedCount = children[index].groupedCount;
            item.groupedSize = children[index].bytes;
            out.push_back(item);
            cursor += span;
        }

        if (horizontal)
        {
            remaining.x += thickness;
            remaining.width = (std::max)(0.0, remaining.width - thickness);
        }
        else
        {
            remaining.y += thickness;
            remaining.height = (std::max)(0.0, remaining.height - thickness);
        }
    }

    std::vector<SizedChild> CollectChildren(
        FileNode const& root,
        TreemapRect const& bounds,
        TreemapOptions const& options)
    {
        std::vector<SizedChild> kept;
        uint64_t groupedBytes = 0;
        size_t groupedCount = 0;
        uint64_t total = 0;

        for (auto const& child : root.children)
        {
            if (child->isDir && !options.showFolders) continue;
            if (!child->isDir && !options.showFiles) continue;
            if (!options.nameFilterLower.empty())
            {
                std::wstring name = child->name;
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                if (name.find(options.nameFilterLower) == std::wstring::npos)
                {
                    continue;
                }
            }
            uint64_t bytes = child->size;
            if (bytes == 0) continue;
            if (options.groupSmallBytes > 0 && bytes < options.groupSmallBytes)
            {
                groupedBytes += bytes;
                groupedCount += 1;
                continue;
            }
            kept.push_back({ child.get(), 0, bytes, false, 0 });
            total += bytes;
        }

        if (options.sortByName)
        {
            std::sort(kept.begin(), kept.end(), [](auto const& left, auto const& right) {
                if (!left.node) return false;
                if (!right.node) return true;
                return _wcsicmp(left.node->name.c_str(), right.node->name.c_str()) < 0;
            });
        }
        else
        {
            std::sort(kept.begin(), kept.end(), [](auto const& left, auto const& right) {
                return left.bytes > right.bytes;
            });
        }

        if (kept.size() > options.maxRects)
        {
            for (size_t index = options.maxRects; index < kept.size(); ++index)
            {
                groupedBytes += kept[index].bytes;
                groupedCount += 1;
            }
            kept.resize(options.maxRects);
        }

        total = 0;
        for (auto const& item : kept) total += item.bytes;
        total += groupedBytes;
        if (groupedCount > 0 && groupedBytes > 0)
        {
            kept.push_back({ nullptr, 0, groupedBytes, true, groupedCount });
        }

        double available = (std::max)(0.0, bounds.Area());
        if (total == 0)
        {
            return {};
        }
        else
        {
            for (auto& item : kept)
            {
                item.area = available * (static_cast<double>(item.bytes) / static_cast<double>(total));
            }
        }
        return kept;
    }
}

namespace Orbit::Core
{
    std::vector<TreemapItem> TreemapLayout::Layout(
        FileNode const& root,
        TreemapRect bounds,
        TreemapOptions const& options)
    {
        std::vector<TreemapItem> result;
        if (bounds.width < options.minPixel || bounds.height < options.minPixel)
        {
            return result;
        }

        auto children = CollectChildren(root, bounds, options);
        if (children.empty())
        {
            return result;
        }

        TreemapRect remaining = bounds;
        std::vector<size_t> row;
        std::vector<double> rowAreas;
        for (size_t index = 0; index < children.size(); ++index)
        {
            double shortest = (std::min)(remaining.width, remaining.height);
            rowAreas.push_back(children[index].area);
            if (row.empty() || WorstAspect(rowAreas, shortest) <= WorstAspect(
                    std::vector<double>(rowAreas.begin(), rowAreas.end() - 1), shortest))
            {
                row.push_back(index);
            }
            else
            {
                rowAreas.pop_back();
                LayoutRow(children, row, remaining, options.minPixel, result);
                row.clear();
                rowAreas.clear();
                row.push_back(index);
                rowAreas.push_back(children[index].area);
            }
        }
        if (!row.empty())
        {
            LayoutRow(children, row, remaining, options.minPixel, result);
        }
        return result;
    }

    bool TreemapLayout::SelfTest() noexcept
    {
        FileNode root;
        root.isDir = true;
        uint64_t sizes[] = { 6, 6, 4, 3, 2 };
        for (uint64_t size : sizes)
        {
            auto child = std::make_unique<FileNode>();
            child->name = L"n" + std::to_wstring(size);
            child->size = size;
            child->isDir = true;
            root.children.push_back(std::move(child));
        }

        TreemapRect bounds{ 0, 0, 210, 100 };
        auto layout = Layout(root, bounds, {});
        if (layout.size() != 5)
        {
            return false;
        }

        double totalArea = 0;
        for (auto const& item : layout)
        {
            auto const& rect = item.bounds;
            if (rect.x < -0.01 || rect.y < -0.01 ||
                rect.Right() > bounds.width + 0.5 ||
                rect.Bottom() > bounds.height + 0.5)
            {
                return false;
            }
            totalArea += rect.Area();
            if (!item.node || item.node->size == 0) continue;
            double expected = bounds.Area() * (static_cast<double>(item.node->size) / 21.0);
            if (std::abs(rect.Area() - expected) / expected > 0.35)
            {
                return false;
            }
        }
        for (size_t i = 0; i < layout.size(); ++i)
        {
            for (size_t j = i + 1; j < layout.size(); ++j)
            {
                if (Overlaps(layout[i].bounds, layout[j].bounds, 0.01))
                {
                    return false;
                }
            }
        }
        return std::abs(totalArea - bounds.Area()) / bounds.Area() < 0.15;
    }
}
