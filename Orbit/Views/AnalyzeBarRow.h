#pragma once

#include "../Platform/ShellOperations.h"

#include <winrt/Windows.UI.Text.h>

#include <algorithm>
#include <string>

namespace Orbit::Views
{

inline wchar_t const* LocationGlyph(std::wstring const& name) noexcept
{
    if (name == L"This disk") return L"\uEDA2";
    if (name == L"Home") return L"\uE10F";
    if (name == L"User Library") return L"\uE8F1";
    if (name.rfind(L"Applications", 0) == 0) return L"\uE74C";
    if (name == L"Program Files" || name == L"Program Files (x86)") return L"\uE74C";
    if (name == L"Windows") return L"\uE799";
    if (name == L"Users") return L"\uE77B";
    if (name == L"Downloads") return L"\uE896";
    return L"\uE8B7";
}

inline void BindLocationCardHover(winrt::Microsoft::UI::Xaml::Controls::Border const& card)
{
    using namespace winrt;
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Input;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    auto lookup = [](wchar_t const* key) -> Brush {
        auto value = Application::Current().Resources().TryLookup(box_value(key));
        return value ? value.try_as<Brush>() : Brush{ nullptr };
    };
    Brush idleBg = card.Background();
    Brush idleStroke = card.BorderBrush();
    Brush hoverBg = lookup(L"CardBackgroundFillColorSecondaryBrush");
    if (!hoverBg) hoverBg = lookup(L"SubtleFillColorSecondaryBrush");
    Brush pressBg = lookup(L"SubtleFillColorTertiaryBrush");
    if (!pressBg) pressBg = hoverBg;
    Brush hoverStroke = lookup(L"OrbitJupiterBrush");
    if (!hoverStroke) hoverStroke = idleStroke;

    card.PointerEntered([card, hoverBg, hoverStroke](auto const&, auto const&) {
        if (hoverBg) card.Background(hoverBg);
        if (hoverStroke) card.BorderBrush(hoverStroke);
    });
    card.PointerExited([card, idleBg, idleStroke](auto const&, auto const&) {
        card.Background(idleBg);
        card.BorderBrush(idleStroke);
    });
    card.PointerPressed([card, pressBg, hoverStroke](auto const&, auto const&) {
        if (pressBg) card.Background(pressBg);
        if (hoverStroke) card.BorderBrush(hoverStroke);
    });
    card.PointerReleased([card, hoverBg, hoverStroke](auto const&, auto const&) {
        if (hoverBg) card.Background(hoverBg);
        if (hoverStroke) card.BorderBrush(hoverStroke);
    });
}

inline winrt::Microsoft::UI::Xaml::Controls::Grid MakeUsageBarRow(
    std::wstring const& path,
    std::wstring const& name,
    uint64_t size,
    uint64_t totalBytes,
    int /*index*/ = 0)
{
    using namespace winrt;
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    auto resource = [](wchar_t const* key) {
        return Application::Current().Resources().Lookup(box_value(key));
    };

    Grid root;
    root.Tag(box_value(hstring(path)));
    root.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));

    Border card;
    card.Style(resource(L"MoleCardStyle").as<Style>());
    card.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
    card.Margin(ThicknessHelper::FromUniformLength(0));

    Grid grid;
    grid.ColumnSpacing(12);
    grid.MinHeight(56);
    ColumnDefinition iconCol;
    iconCol.Width(GridLengthHelper::Auto());
    ColumnDefinition textCol;
    textCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition sizeCol;
    sizeCol.Width(GridLengthHelper::FromPixels(112));
    ColumnDefinition chevronCol;
    chevronCol.Width(GridLengthHelper::Auto());
    grid.ColumnDefinitions().Append(iconCol);
    grid.ColumnDefinitions().Append(textCol);
    grid.ColumnDefinitions().Append(sizeCol);
    grid.ColumnDefinitions().Append(chevronCol);

    Border iconHost;
    iconHost.Style(resource(L"CleanCategoryIconHostStyle").as<Style>());
    iconHost.Background(resource(L"OrbitJupiterChipBackgroundBrush").as<Brush>());
    FontIcon icon;
    icon.Glyph(LocationGlyph(name));
    icon.FontSize(16);
    icon.Foreground(resource(L"OrbitJupiterBrush").as<Brush>());
    icon.HorizontalAlignment(HorizontalAlignment::Center);
    icon.VerticalAlignment(VerticalAlignment::Center);
    iconHost.Child(icon);

    double ratio = totalBytes > 0
        ? static_cast<double>(size) / static_cast<double>(totalBytes)
        : 0;

    StackPanel titlePanel;
    titlePanel.Spacing(2);
    titlePanel.VerticalAlignment(VerticalAlignment::Center);
    TextBlock title;
    title.Text(hstring(name));
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    title.FontSize(14);
    title.TextTrimming(TextTrimming::CharacterEllipsis);
    titlePanel.Children().Append(title);

    TextBlock subtitle;
    subtitle.Text(hstring(path));
    subtitle.Style(resource(L"OrbitBodySecondaryStyle").as<Style>());
    subtitle.FontSize(12);
    subtitle.TextTrimming(TextTrimming::CharacterEllipsis);
    titlePanel.Children().Append(subtitle);

    if (size > 0)
    {
        Border barHost;
        barHost.Height(4);
        barHost.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));
        barHost.CornerRadius(CornerRadius{ 2, 2, 2, 2 });
        barHost.Background(resource(L"CardStrokeColorDefaultBrush").as<Brush>());
        Border fill;
        fill.Width((std::max)(6.0, 220.0 * ratio));
        fill.HorizontalAlignment(HorizontalAlignment::Left);
        fill.Background(resource(L"OrbitJupiterBrush").as<Brush>());
        barHost.Child(fill);
        titlePanel.Children().Append(barHost);
    }

    StackPanel sizePanel;
    sizePanel.VerticalAlignment(VerticalAlignment::Center);
    TextBlock bytes;
    bytes.Style(resource(L"CleanImpactSizeStyle").as<Style>());
    bytes.Text(hstring(size == 0
        ? L""
        : Platform::ShellOperations::FormatBytes(size)));
    sizePanel.Children().Append(bytes);
    if (size > 0 && totalBytes > 0)
    {
        wchar_t percent[16]{};
        swprintf_s(percent, L"%.1f%%", 100.0 * ratio);
        TextBlock pct;
        pct.Text(hstring(percent));
        pct.Style(resource(L"OrbitBodySecondaryStyle").as<Style>());
        pct.FontSize(12);
        pct.HorizontalAlignment(HorizontalAlignment::Right);
        sizePanel.Children().Append(pct);
    }

    FontIcon chevron;
    chevron.Glyph(L"\uE76C");
    chevron.FontSize(12);
    chevron.Opacity(0.45);
    chevron.VerticalAlignment(VerticalAlignment::Center);

    Grid::SetColumn(titlePanel, 1);
    Grid::SetColumn(sizePanel, 2);
    Grid::SetColumn(chevron, 3);
    grid.Children().Append(iconHost);
    grid.Children().Append(titlePanel);
    grid.Children().Append(sizePanel);
    grid.Children().Append(chevron);
    card.Child(grid);
    BindLocationCardHover(card);
    root.Children().Append(card);
    return root;
}

inline winrt::Microsoft::UI::Xaml::Controls::Grid MakeSidebarRow(
    std::wstring const& path,
    std::wstring const& name,
    uint64_t size,
    uint64_t totalBytes)
{
    using namespace winrt;
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    auto resource = [](wchar_t const* key) {
        return Application::Current().Resources().Lookup(box_value(key));
    };

    Grid grid;
    grid.Tag(box_value(hstring(path)));
    grid.Padding(ThicknessHelper::FromLengths(8, 8, 8, 8));
    grid.ColumnSpacing(10);
    ColumnDefinition iconCol; iconCol.Width(GridLengthHelper::Auto());
    ColumnDefinition textCol; textCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition sizeCol; sizeCol.Width(GridLengthHelper::FromPixels(72));
    ColumnDefinition chevronCol; chevronCol.Width(GridLengthHelper::Auto());
    grid.ColumnDefinitions().Append(iconCol);
    grid.ColumnDefinitions().Append(textCol);
    grid.ColumnDefinitions().Append(sizeCol);
    grid.ColumnDefinitions().Append(chevronCol);

    FontIcon icon;
    icon.Glyph(LocationGlyph(name));
    icon.FontSize(16);
    icon.Foreground(resource(L"OrbitJupiterBrush").as<Brush>());
    icon.VerticalAlignment(VerticalAlignment::Center);

    StackPanel titlePanel;
    titlePanel.Spacing(0);
    titlePanel.VerticalAlignment(VerticalAlignment::Center);
    TextBlock title;
    title.Text(hstring(name));
    title.FontSize(13);
    title.TextTrimming(TextTrimming::CharacterEllipsis);
    titlePanel.Children().Append(title);
    TextBlock bytes;
    bytes.Text(hstring(size == 0 ? L"" : Platform::ShellOperations::FormatBytes(size)));
    bytes.Style(resource(L"OrbitBodySecondaryStyle").as<Style>());
    bytes.FontSize(11);
    titlePanel.Children().Append(bytes);

    TextBlock pct;
    pct.HorizontalAlignment(HorizontalAlignment::Right);
    pct.VerticalAlignment(VerticalAlignment::Center);
    pct.Style(resource(L"OrbitBodySecondaryStyle").as<Style>());
    pct.FontSize(11);
    if (size > 0 && totalBytes > 0)
    {
        wchar_t text[16]{};
        swprintf_s(text, L"%.0f%%", 100.0 * static_cast<double>(size) / static_cast<double>(totalBytes));
        pct.Text(hstring(text));
    }

    FontIcon chevron;
    chevron.Glyph(L"\uE76C");
    chevron.FontSize(10);
    chevron.Opacity(0.4);
    chevron.VerticalAlignment(VerticalAlignment::Center);

    Grid::SetColumn(titlePanel, 1);
    Grid::SetColumn(pct, 2);
    Grid::SetColumn(chevron, 3);
    grid.Children().Append(icon);
    grid.Children().Append(titlePanel);
    grid.Children().Append(pct);
    grid.Children().Append(chevron);
    return grid;
}

inline void UpdateSidebarRow(
    winrt::Microsoft::UI::Xaml::Controls::Grid const& grid,
    uint64_t size,
    uint64_t totalBytes)
{
    using namespace winrt;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    if (grid.Children().Size() < 3) return;
    auto titlePanel = grid.Children().GetAt(1).try_as<StackPanel>();
    if (!titlePanel || titlePanel.Children().Size() < 2) return;
    auto bytes = titlePanel.Children().GetAt(1).try_as<TextBlock>();
    auto pct = grid.Children().GetAt(2).try_as<TextBlock>();
    if (bytes)
    {
        bytes.Text(hstring(size == 0
            ? L""
            : Platform::ShellOperations::FormatBytes(size)));
    }
    if (pct)
    {
        if (size > 0 && totalBytes > 0)
        {
            wchar_t text[16]{};
            swprintf_s(
                text,
                L"%.0f%%",
                100.0 * static_cast<double>(size) / static_cast<double>(totalBytes));
            pct.Text(hstring(text));
        }
        else pct.Text(L"");
    }
}

} // namespace Orbit::Views
