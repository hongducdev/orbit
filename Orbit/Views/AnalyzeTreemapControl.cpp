#include "pch.h"
#include "AnalyzeTreemapControl.h"

#include "../Platform/ShellOperations.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

#include <unordered_map>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Animation;
using winrt::Microsoft::UI::Xaml::Automation::AutomationProperties;
using winrt::Windows::System::VirtualKeyModifiers;

namespace
{
    template <typename T>
    T Resource(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(box_value(key)).as<T>();
    }

    wchar_t const* PaletteKey(std::wstring const& name, bool placeholder, bool cloud)
    {
        if (placeholder) return L"OrbitTreemapOtherBrush";
        if (cloud) return L"OrbitTreemapCloudBrush";
        wchar_t const* keys[] = {
            L"OrbitTreemapTanBrush",
            L"OrbitTreemapOchreBrush",
            L"OrbitTreemapPurpleBrush",
            L"OrbitTreemapTerraBrush",
            L"OrbitTreemapSageBrush",
            L"OrbitTreemapSlateBrush"
        };
        size_t hash = 0;
        for (wchar_t ch : name) hash = hash * 101 + static_cast<size_t>(ch);
        return keys[hash % 6];
    }

    void AnimateTo(Border const& tile, double x, double y, double w, double h, bool fadeIn)
    {
        auto span = winrt::Windows::Foundation::TimeSpan{ 1800000 }; // 180ms
        auto run = [&](wchar_t const* prop, double to) {
            DoubleAnimation anim;
            anim.To(to);
            anim.Duration(DurationHelper::FromTimeSpan(span));
            anim.EnableDependentAnimation(true);
            Storyboard board;
            Storyboard::SetTarget(anim, tile);
            Storyboard::SetTargetProperty(anim, prop);
            board.Children().Append(anim);
            board.Begin();
        };
        run(L"(Canvas.Left)", x);
        run(L"(Canvas.Top)", y);
        run(L"Width", w);
        run(L"Height", h);
        if (fadeIn)
        {
            tile.Opacity(0);
            DoubleAnimation fade;
            fade.To(1.0);
            fade.Duration(DurationHelper::FromTimeSpan(span));
            Storyboard board;
            Storyboard::SetTarget(fade, tile);
            Storyboard::SetTargetProperty(fade, L"Opacity");
            board.Children().Append(fade);
            board.Begin();
        }
    }

    double TileGap(double width, double height) noexcept
    {
        double shortest = (std::min)(width, height);
        if (shortest < 16.0) return 1.0;
        if (shortest < 36.0) return 2.0;
        return 3.0;
    }

    double TileRadius(double width, double height) noexcept
    {
        double shortest = (std::min)(width, height);
        double radius = shortest * 0.18;
        if (radius > 8.0) radius = 8.0;
        if (radius < 1.5) radius = shortest < 8.0 ? 1.0 : 2.0;
        if (radius * 2.0 > shortest - 1.0) radius = (std::max)(1.0, (shortest - 1.0) * 0.5);
        return radius;
    }

    void FillTile(
        Border const& tile,
        std::wstring const& name,
        uint64_t size,
        double width,
        double height,
        bool selected,
        bool placeholder,
        bool cloud)
    {
        tile.Background(Resource<Brush>(PaletteKey(name, placeholder, cloud)));
        double radius = TileRadius(width, height);
        tile.CornerRadius(CornerRadius{ radius, radius, radius, radius });
        if (selected)
        {
            tile.BorderThickness(ThicknessHelper::FromUniformLength(2));
            tile.BorderBrush(Resource<Brush>(L"OrbitPrimaryBrush"));
        }
        else
        {
            tile.BorderThickness(ThicknessHelper::FromUniformLength(0));
        }

        if (width < 72 || height < 48)
        {
            tile.Child(nullptr);
            return;
        }
        StackPanel stack;
        stack.VerticalAlignment(VerticalAlignment::Center);
        stack.HorizontalAlignment(HorizontalAlignment::Center);
        stack.Spacing(2);
        if (height > 72)
        {
            FontIcon icon;
            icon.Glyph(L"\uE8B7");
            icon.FontSize(18);
            icon.Foreground(SolidColorBrush(Windows::UI::Colors::White()));
            icon.HorizontalAlignment(HorizontalAlignment::Center);
            stack.Children().Append(icon);
        }
        TextBlock label;
        label.Text(hstring(name));
        label.FontSize(12);
        label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        label.Foreground(SolidColorBrush(Windows::UI::Colors::White()));
        label.TextAlignment(TextAlignment::Center);
        label.TextWrapping(TextWrapping::NoWrap);
        label.TextTrimming(TextTrimming::CharacterEllipsis);
        stack.Children().Append(label);
        if (height > 64)
        {
            TextBlock bytes;
            bytes.Text(hstring(::Orbit::Platform::ShellOperations::FormatBytes(size)));
            bytes.FontSize(11);
            bytes.Opacity(0.85);
            bytes.Foreground(SolidColorBrush(Windows::UI::Colors::White()));
            bytes.TextAlignment(TextAlignment::Center);
            stack.Children().Append(bytes);
        }
        tile.Child(stack);
    }
}

namespace Orbit::Views
{
    void AnalyzeTreemapControl::Render(
        Canvas const& canvas,
        std::vector<Core::TreemapItem> const& items,
        uint64_t parentSize,
        std::unordered_set<std::wstring> const& selected,
        TreemapRenderCallbacks const& callbacks)
    {
        std::unordered_map<std::wstring, Border> existing;
        for (auto const& child : canvas.Children())
        {
            auto border = child.try_as<Border>();
            if (!border) continue;
            auto key = unbox_value_or<hstring>(border.Tag(), L"");
            if (!key.empty()) existing.emplace(std::wstring(key), border);
        }

        std::unordered_set<std::wstring> used;
        for (auto const& item : items)
        {
            auto bounds = item.bounds;
            double gap = TileGap(bounds.width, bounds.height);
            bounds.x += gap;
            bounds.y += gap;
            bounds.width = (std::max)(0.0, bounds.width - gap * 2.0);
            bounds.height = (std::max)(0.0, bounds.height - gap * 2.0);
            if (bounds.width < 1 || bounds.height < 1) continue;

            std::wstring name = item.isGroupPlaceholder
                ? (std::to_wstring(item.groupedCount) + L" smaller items")
                : (item.node ? item.node->name : L"");
            std::wstring path = item.isGroupPlaceholder
                ? L"__group__"
                : (item.node ? item.node->path : L"");
            uint64_t size = item.isGroupPlaceholder
                ? item.groupedSize
                : (item.node ? item.node->size : 0);
            double percent = parentSize > 0
                ? (100.0 * static_cast<double>(size) / static_cast<double>(parentSize))
                : 0;
            bool isSelected = item.node && selected.contains(item.node->path);
            used.insert(path);

            Border tile{ nullptr };
            bool created = false;
            auto found = existing.find(path);
            if (found != existing.end())
            {
                tile = found->second;
            }
            else
            {
                tile = Border();
                tile.Style(Resource<Style>(L"AnalyzeTreemapTileStyle"));
                tile.Tag(box_value(hstring(path)));
                Canvas::SetLeft(tile, bounds.x);
                Canvas::SetTop(tile, bounds.y);
                tile.Width((std::max)(1.0, bounds.width * 0.6));
                tile.Height((std::max)(1.0, bounds.height * 0.6));
                canvas.Children().Append(tile);
                created = true;

                bool isDir = item.node && item.node->isDir;
                bool placeholder = item.isGroupPlaceholder;
                tile.PointerPressed([callbacks, itemPath = path, isDir, placeholder](
                    auto const&, PointerRoutedEventArgs const& args) {
                    if (placeholder) return;
                    if (!args.GetCurrentPoint(nullptr).Properties().IsLeftButtonPressed()) return;
                    auto mods = args.KeyModifiers();
                    bool ctrl = (mods & VirtualKeyModifiers::Control) != VirtualKeyModifiers::None;
                    if (ctrl && callbacks.onTapped)
                    {
                        callbacks.onTapped(itemPath, true);
                        args.Handled(true);
                    }
                    else if (isDir && callbacks.onDrill)
                    {
                        callbacks.onDrill(itemPath);
                        args.Handled(true);
                    }
                    else if (callbacks.onTapped)
                    {
                        callbacks.onTapped(itemPath, false);
                        args.Handled(true);
                    }
                });
                tile.RightTapped([callbacks, itemPath = path, placeholder](
                    auto const&, RightTappedRoutedEventArgs const&) {
                    if (!placeholder && callbacks.onRightTapped) callbacks.onRightTapped(itemPath);
                });
            }

            wchar_t tooltip[512]{};
            swprintf_s(
                tooltip,
                L"%s\n%s (%.1f%% of parent)",
                name.c_str(),
                ::Orbit::Platform::ShellOperations::FormatBytes(size).c_str(),
                percent);
            ToolTipService::SetToolTip(tile, box_value(tooltip));
            AutomationProperties::SetName(tile, hstring(tooltip));
            bool cloud = item.node && item.node->isCloudPlaceholder;
            FillTile(
                tile, name, size, bounds.width, bounds.height, isSelected,
                item.isGroupPlaceholder, cloud);
            AnimateTo(tile, bounds.x, bounds.y, bounds.width, bounds.height, created);
        }

        auto children = canvas.Children();
        for (int32_t i = static_cast<int32_t>(children.Size()) - 1; i >= 0; --i)
        {
            auto border = children.GetAt(static_cast<uint32_t>(i)).try_as<Border>();
            if (!border) continue;
            auto key = std::wstring(unbox_value_or<hstring>(border.Tag(), L""));
            if (!used.contains(key)) children.RemoveAt(static_cast<uint32_t>(i));
        }
    }
}
