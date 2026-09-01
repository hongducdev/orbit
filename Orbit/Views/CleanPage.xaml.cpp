#include "pch.h"
#include "CleanPage.xaml.h"
#if __has_include("Views/CleanPage.g.cpp")
#include "Views/CleanPage.g.cpp"
#elif __has_include("CleanPage.g.cpp")
#include "CleanPage.g.cpp"
#endif

#include "../Helpers/AppSettings.h"

#include <algorithm>
#include <numeric>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Microsoft::UI::Xaml::Automation::AutomationProperties;
using winrt::Microsoft::UI::Xaml::Automation::Peers::AutomationHeadingLevel;

namespace
{
    void AddColumn(Grid const& grid, GridLength const& width)
    {
        ColumnDefinition column;
        column.Width(width);
        grid.ColumnDefinitions().Append(column);
    }

    IReference<bool> BoxedBoolean(bool value)
    {
        return box_value(value).as<IReference<bool>>();
    }
}

namespace winrt::Orbit::implementation
{
    CleanPage::CleanPage() :
        m_viewModel(std::make_unique<::Orbit::ViewModels::CleanViewModel>())
    {
        m_visibleFileLimits.fill(kInitialVisibleFiles);
        Loaded([this](auto&&, auto&&) {
            UpdateControls();
            if (::Orbit::Helpers::AppSettings::DeleteMode() == L"permanent")
            {
                ShowFeedback(
                    L"Permanent deletion enabled",
                    L"Cleaning will bypass the Recycle Bin and requires two confirmations.",
                    InfoBarSeverity::Warning);
            }
        });
    }

    void CleanPage::ScanButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StartScanAsync();
    }

    void CleanPage::CancelButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_viewModel->CancelScan();
        ScanStatusText().Text(m_viewModel->scanStatus);
        CancelButton().IsEnabled(false);
    }

    void CleanPage::SearchBox_TextChanged(
        IInspectable const&,
        AutoSuggestBoxTextChangedEventArgs const&)
    {
        if (m_rendering) return;
        m_searchText = SearchBox().Text().c_str();
        if (m_hasScanned) RenderResults();
    }

    void CleanPage::RiskyToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_rendering) return;
        m_showRisky = RiskyToggle().IsOn();
        if (!m_showRisky)
        {
            m_viewModel->DeselectRisky();
        }
        if (m_hasScanned)
        {
            RenderResults();
            UpdateControls();
        }
    }

    void CleanPage::AddWhitelist_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto pattern = WhitelistPatternBox().Text();
        if (pattern.empty())
        {
            ShowFeedback(
                L"Protection pattern required",
                L"Enter an absolute path or glob pattern.",
                InfoBarSeverity::Error);
            return;
        }
        if (!m_viewModel->AddWhitelistPattern(pattern.c_str()))
        {
            ShowFeedback(
                L"Protection was not saved",
                L"Orbit could not persist the whitelist. Check Local AppData access and try again.",
                InfoBarSeverity::Error);
            return;
        }
        WhitelistPatternBox().Text(L"");
        WhitelistButton().Flyout().Hide();
        ShowFeedback(
            L"Protection saved",
            L"The path or pattern will be skipped on the next scan.",
            InfoBarSeverity::Success);
    }

    void CleanPage::CleanButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ConfirmAndCleanAsync();
    }

    fire_and_forget CleanPage::StartScanAsync()
    {
        auto lifetime = get_strong();
        try
        {
            auto operation = m_viewModel->ScanAsync();
            UpdateControls();
            co_await operation;
            m_hasScanned = true;
            RenderResults();
            UpdateControls();

            if (m_viewModel->cancelRequested.load())
            {
                ShowFeedback(
                    L"Scan cancelled",
                    L"Partial results are shown and can be reviewed.",
                    InfoBarSeverity::Warning);
            }
            else if (m_viewModel->scanStatus.rfind(L"Scan failed", 0) == 0)
            {
                ShowFeedback(
                    L"Scan failed",
                    hstring(m_viewModel->scanStatus),
                    InfoBarSeverity::Error);
            }
        }
        catch (hresult_error const& error)
        {
            UpdateControls();
            ShowFeedback(
                L"Scan failed",
                error.message(),
                InfoBarSeverity::Error);
        }
    }

    fire_and_forget CleanPage::ConfirmAndCleanAsync()
    {
        auto lifetime = get_strong();
        if (!m_viewModel->CanDelete()) co_return;
        if (!m_searchText.empty())
        {
            ShowFeedback(
                L"Clear search before cleaning",
                L"Cleaning is disabled while results are filtered so hidden selections cannot be deleted.",
                InfoBarSeverity::Warning);
            co_return;
        }

        bool permanent =
            ::Orbit::Helpers::AppSettings::DeleteMode() == L"permanent";
        wchar_t summary[256]{};
        swprintf_s(
            summary,
            L"%u selected items (%s). %s",
            m_viewModel->selectedFiles,
            m_viewModel->SelectedFormatted().c_str(),
            permanent
                ? L"This cannot be undone."
                : L"They can be restored from the Recycle Bin.");

        ContentDialog confirmation;
        confirmation.XamlRoot(XamlRoot());
        confirmation.Title(box_value(
            permanent
                ? L"Review permanent deletion"
                : L"Move selected items to Recycle Bin?"));
        confirmation.Content(box_value(hstring(summary)));
        confirmation.PrimaryButtonText(permanent ? L"Continue" : L"Move to Recycle Bin");
        confirmation.CloseButtonText(L"Cancel");
        confirmation.DefaultButton(ContentDialogButton::Close);
        if (co_await confirmation.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        if (permanent)
        {
            ContentDialog secondConfirmation;
            secondConfirmation.XamlRoot(XamlRoot());
            secondConfirmation.Title(box_value(L"Permanently delete selected items?"));
            secondConfirmation.Content(box_value(
                L"Orbit will bypass the Recycle Bin. This action cannot be undone."));
            secondConfirmation.PrimaryButtonText(L"Permanently delete");
            secondConfirmation.CloseButtonText(L"Cancel");
            secondConfirmation.DefaultButton(ContentDialogButton::Close);
            if (co_await secondConfirmation.ShowAsync() != ContentDialogResult::Primary)
            {
                co_return;
            }
        }

        auto operation = m_viewModel->DeleteSelectedAsync(permanent);
        UpdateControls();
        co_await operation;
        RenderResults();
        UpdateControls();

        auto const& result = m_viewModel->lastDeleteResult;
        if (result.succeeded)
        {
            wchar_t message[256]{};
            swprintf_s(
                message,
                L"%zu items cleaned. %s",
                result.completedCount,
                permanent
                    ? L"Files were permanently deleted."
                    : L"Use File Explorer to restore recycled files.");
            ShowFeedback(
                L"Clean complete",
                message,
                InfoBarSeverity::Success);
        }
        else
        {
            ShowFeedback(
                result.IsPartial() ? L"Clean partially completed" : L"Clean failed",
                result.error.empty()
                    ? hstring(L"Windows did not complete the operation.")
                    : hstring(result.error),
                result.IsPartial() ? InfoBarSeverity::Warning : InfoBarSeverity::Error);
        }
    }

    void CleanPage::RenderResults()
    {
        m_rendering = true;
        CategoryList().Items().Clear();

        std::vector<size_t> categoryIndices(m_viewModel->categories.size());
        std::iota(categoryIndices.begin(), categoryIndices.end(), 0);
        std::stable_sort(
            categoryIndices.begin(),
            categoryIndices.end(),
            [&](size_t left, size_t right) {
                return m_viewModel->categories[left].totalBytes >
                    m_viewModel->categories[right].totalBytes;
            });

        for (size_t index : categoryIndices)
        {
            auto const& category = m_viewModel->categories[index];
            if (!m_viewModel->CategoryMatchesSearch(category, m_searchText))
            {
                continue;
            }
            CategoryList().Items().Append(BuildCategory(index));
        }

        bool hasVisibleCategories = CategoryList().Items().Size() > 0;
        CategoryList().Visibility(
            hasVisibleCategories ? Visibility::Visible : Visibility::Collapsed);
        EmptyState().Visibility(
            hasVisibleCategories ? Visibility::Collapsed : Visibility::Visible);

        if (!hasVisibleCategories)
        {
            if (!m_searchText.empty())
            {
                EmptyStateTitle().Text(L"No matching results");
                EmptyStateMessage().Text(
                    L"No category or scanned path matches the current search.");
            }
            else
            {
                EmptyStateTitle().Text(L"No cleanable cache found");
                EmptyStateMessage().Text(
                    L"Orbit found no eligible files. Protected and unavailable paths remain untouched.");
            }
        }

        m_rendering = false;
    }

    void CleanPage::UpdateControls()
    {
        bool scanning = m_viewModel->isScanning.load();
        bool deleting = m_viewModel->isDeleting.load();
        bool busy = scanning || deleting;
        bool permanent =
            ::Orbit::Helpers::AppSettings::DeleteMode() == L"permanent";

        ScanButton().IsEnabled(!busy);
        CancelButton().Visibility(scanning ? Visibility::Visible : Visibility::Collapsed);
        CancelButton().IsEnabled(scanning && !m_viewModel->cancelRequested.load());
        ScanProgress().Visibility(scanning ? Visibility::Visible : Visibility::Collapsed);
        SearchBox().IsEnabled(!busy);
        RiskyToggle().IsEnabled(!busy);
        WhitelistButton().IsEnabled(!busy);
        CategoryList().IsEnabled(!busy);

        ActionBar().Visibility(m_hasScanned ? Visibility::Visible : Visibility::Collapsed);
        wchar_t selection[128]{};
        swprintf_s(
            selection,
            L"Selected %u items · %s",
            m_viewModel->selectedFiles,
            m_viewModel->SelectedFormatted().c_str());
        SelectionSummary().Text(selection);
        ScanStatusText().Text(hstring(m_viewModel->scanStatus));
        CleanButton().Content(box_value(
            deleting
                ? L"Cleaning…"
                : (permanent ? L"Permanently delete" : L"Move to Recycle Bin")));
        CleanButton().IsEnabled(
            !busy && m_searchText.empty() && m_viewModel->CanDelete());
        AutomationProperties::SetName(
            CleanButton(),
            hstring(L"Clean ") + to_hstring(m_viewModel->selectedFiles) +
                L" selected items, " + m_viewModel->SelectedFormatted());
    }

    void CleanPage::ShowFeedback(
        hstring const& title,
        hstring const& message,
        InfoBarSeverity severity)
    {
        FeedbackBar().Title(title);
        FeedbackBar().Message(message);
        FeedbackBar().Severity(severity);
        FeedbackBar().IsOpen(true);
    }

    Expander CleanPage::BuildCategory(size_t categoryIndex)
    {
        auto const& category = m_viewModel->categories[categoryIndex];
        Grid header;
        header.ColumnSpacing(10);
        AddColumn(header, GridLengthHelper::Auto());
        AddColumn(header, GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        AddColumn(header, GridLengthHelper::Auto());
        AddColumn(header, GridLengthHelper::Auto());

        CheckBox selectAll;
        selectAll.IsThreeState(true);
        selectAll.IsEnabled(category.recyclable && !category.files.empty());
        uint32_t selected = category.SelectedCount();
        if (selected == 0)
        {
            selectAll.IsChecked(BoxedBoolean(false));
        }
        else if (selected == category.fileCount)
        {
            selectAll.IsChecked(BoxedBoolean(true));
        }
        else
        {
            selectAll.IsChecked(nullptr);
        }
        selectAll.Tag(box_value(static_cast<uint32_t>(categoryIndex)));
        AutomationProperties::SetName(
            selectAll,
            hstring(L"Select all eligible items in ") + hstring(category.displayName));
        selectAll.Click([this](IInspectable const& sender, RoutedEventArgs const&) {
            if (m_rendering) return;
            auto checkBox = sender.as<CheckBox>();
            size_t index = unbox_value<uint32_t>(checkBox.Tag());
            auto checked = checkBox.IsChecked();
            m_viewModel->SelectCategory(
                index,
                checked && checked.Value(),
                m_showRisky);
            RenderResults();
            UpdateControls();
        });
        header.Children().Append(selectAll);

        StackPanel titlePanel;
        titlePanel.Spacing(2);
        TextBlock title;
        title.Text(hstring(category.displayName));
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        AutomationProperties::SetHeadingLevel(
            title,
            AutomationHeadingLevel::Level2);
        titlePanel.Children().Append(title);
        TextBlock description;
        description.Text(hstring(category.description));
        description.Style(
            Application::Current().Resources().Lookup(
                box_value(L"OrbitBodySecondaryStyle"))
                .as<winrt::Microsoft::UI::Xaml::Style>());
        description.TextTrimming(TextTrimming::CharacterEllipsis);
        titlePanel.Children().Append(description);
        Grid::SetColumn(titlePanel, 1);
        header.Children().Append(titlePanel);

        TextBlock tier;
        tier.Text(TierLabel(category.tier));
        tier.Foreground(TierBrush(category.tier));
        tier.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        Grid::SetColumn(tier, 2);
        header.Children().Append(tier);

        TextBlock impact;
        impact.Text(hstring(
            ::Orbit::Platform::ShellOperations::FormatBytes(category.totalBytes) +
            L" · " + std::to_wstring(category.fileCount) + L" files"));
        impact.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(impact, 3);
        header.Children().Append(impact);

        Expander expander;
        expander.Header(header);
        expander.IsExpanded(m_expandedCategories[categoryIndex]);
        if (m_expandedCategories[categoryIndex])
        {
            expander.Content(BuildCategoryContent(categoryIndex));
        }
        expander.Expanding([this, categoryIndex](
            Expander const& sender,
            ExpanderExpandingEventArgs const&) {
            m_expandedCategories[categoryIndex] = true;
            sender.Content(BuildCategoryContent(categoryIndex));
        });
        expander.Collapsed([this, categoryIndex](
            Expander const&,
            ExpanderCollapsedEventArgs const&) {
            m_expandedCategories[categoryIndex] = false;
        });
        expander.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
        return expander;
    }

    StackPanel CleanPage::BuildCategoryContent(size_t categoryIndex)
    {
        auto const& category = m_viewModel->categories[categoryIndex];
        StackPanel panel;
        panel.Spacing(8);

        if (!category.skippedReason.empty())
        {
            TextBlock reason;
            reason.Text(hstring(category.skippedReason));
            reason.Foreground(TierBrush(::Orbit::Core::CleanTier::Review));
            panel.Children().Append(reason);
        }

        ListView files;
        files.SelectionMode(ListViewSelectionMode::None);
        files.MaxHeight(420);
        files.HorizontalContentAlignment(HorizontalAlignment::Stretch);

        std::wstring loweredSearch = m_searchText;
        std::transform(
            loweredSearch.begin(), loweredSearch.end(),
            loweredSearch.begin(), ::towlower);
        std::wstring loweredCategory = category.displayName;
        std::transform(
            loweredCategory.begin(), loweredCategory.end(),
            loweredCategory.begin(), ::towlower);
        bool categoryMatches = loweredSearch.empty() ||
            loweredCategory.find(loweredSearch) != std::wstring::npos;

        size_t matchingCount = 0;
        size_t visibleCount = 0;
        for (size_t fileIndex = 0; fileIndex < category.files.size(); ++fileIndex)
        {
            auto const& file = category.files[fileIndex];
            if (file.tier == ::Orbit::Core::CleanTier::Risky && !m_showRisky)
            {
                continue;
            }
            std::wstring loweredPath = file.path;
            std::transform(
                loweredPath.begin(), loweredPath.end(),
                loweredPath.begin(), ::towlower);
            if (!categoryMatches &&
                loweredPath.find(loweredSearch) == std::wstring::npos)
            {
                continue;
            }
            ++matchingCount;
            if (visibleCount < m_visibleFileLimits[categoryIndex])
            {
                files.Items().Append(BuildFileRow(categoryIndex, fileIndex));
                ++visibleCount;
            }
        }
        if (files.Items().Size() > 0)
        {
            panel.Children().Append(files);
        }

        if (matchingCount > visibleCount)
        {
            Button showMore;
            size_t remaining = matchingCount - visibleCount;
            showMore.Content(box_value(hstring(
                L"Show more (" + std::to_wstring(remaining) + L" remaining)")));
            showMore.HorizontalAlignment(HorizontalAlignment::Left);
            showMore.Click([this, categoryIndex](
                IInspectable const&, RoutedEventArgs const&) {
                m_visibleFileLimits[categoryIndex] += kInitialVisibleFiles;
                RenderResults();
            });
            panel.Children().Append(showMore);
        }

        if (!category.skipped.empty())
        {
            TextBlock skippedHeading;
            skippedHeading.Text(hstring(
                std::to_wstring(category.skipped.size()) + L" paths skipped"));
            skippedHeading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            panel.Children().Append(skippedHeading);

            size_t shown = std::min<size_t>(category.skipped.size(), 10);
            for (size_t index = 0; index < shown; ++index)
            {
                TextBlock skipped;
                skipped.Text(hstring(
                    category.skipped[index].reason + L" — " +
                    category.skipped[index].path));
                skipped.TextTrimming(TextTrimming::CharacterEllipsis);
                skipped.Opacity(0.7);
                ToolTipService::SetToolTip(
                    skipped,
                    box_value(hstring(category.skipped[index].path)));
                panel.Children().Append(skipped);
            }
        }

        if (matchingCount == 0 && category.skipped.empty() &&
            category.skippedReason.empty())
        {
            TextBlock empty;
            empty.Text(L"No matching cleanable files in this category.");
            empty.Opacity(0.7);
            panel.Children().Append(empty);
        }
        return panel;
    }

    Grid CleanPage::BuildFileRow(size_t categoryIndex, size_t fileIndex)
    {
        auto const& category = m_viewModel->categories[categoryIndex];
        auto const& file = category.files[fileIndex];
        Grid row;
        row.ColumnSpacing(10);
        row.Padding(ThicknessHelper::FromLengths(4, 6, 4, 6));
        AddColumn(row, GridLengthHelper::Auto());
        AddColumn(row, GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        AddColumn(row, GridLengthHelper::FromPixels(100));
        AddColumn(row, GridLengthHelper::FromPixels(90));
        AddColumn(row, GridLengthHelper::FromPixels(70));

        CheckBox selected;
        selected.IsEnabled(category.recyclable);
        selected.IsChecked(BoxedBoolean(file.selected));
        uint64_t encoded =
            (static_cast<uint64_t>(categoryIndex) << 32) |
            static_cast<uint64_t>(fileIndex);
        selected.Tag(box_value(encoded));
        std::wstring accessibleName =
            L"Select " + file.path + L", " +
            ::Orbit::Platform::ShellOperations::FormatBytes(file.size) +
            L", " + std::wstring(TierLabel(file.tier).c_str());
        AutomationProperties::SetName(selected, hstring(accessibleName));
        selected.Click([this](IInspectable const& sender, RoutedEventArgs const&) {
            if (m_rendering) return;
            auto checkBox = sender.as<CheckBox>();
            uint64_t value = unbox_value<uint64_t>(checkBox.Tag());
            size_t category = static_cast<size_t>(value >> 32);
            size_t file = static_cast<size_t>(value & 0xffffffffu);
            auto checked = checkBox.IsChecked();
            m_viewModel->SetFileSelected(
                category,
                file,
                checked && checked.Value(),
                m_showRisky);
            RenderResults();
            UpdateControls();
        });
        row.Children().Append(selected);

        TextBlock path;
        path.Text(hstring(file.path));
        path.TextTrimming(TextTrimming::CharacterEllipsis);
        path.VerticalAlignment(VerticalAlignment::Center);
        ToolTipService::SetToolTip(path, box_value(hstring(file.path)));
        Grid::SetColumn(path, 1);
        row.Children().Append(path);

        TextBlock size;
        size.Text(hstring(::Orbit::Platform::ShellOperations::FormatBytes(file.size)));
        size.VerticalAlignment(VerticalAlignment::Center);
        if (file.isHardlink && file.size == 0)
        {
            ToolTipService::SetToolTip(
                size,
                box_value(L"Duplicate hardlink; excluded from reclaimable total"));
        }
        Grid::SetColumn(size, 2);
        row.Children().Append(size);

        TextBlock age;
        age.Text(FormatAge(file.lastWrite));
        age.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(age, 3);
        row.Children().Append(age);

        TextBlock tier;
        tier.Text(TierLabel(file.tier));
        tier.Foreground(TierBrush(file.tier));
        tier.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(tier, 4);
        row.Children().Append(tier);

        MenuFlyout menu;
        MenuFlyoutItem protect;
        protect.Text(L"Protect this path from future scans");
        protect.Tag(box_value(encoded));
        protect.Click([this](IInspectable const& sender, RoutedEventArgs const&) {
            auto item = sender.as<MenuFlyoutItem>();
            uint64_t value = unbox_value<uint64_t>(item.Tag());
            size_t category = static_cast<size_t>(value >> 32);
            size_t file = static_cast<size_t>(value & 0xffffffffu);
            m_viewModel->WhitelistFile(category, file);
            RenderResults();
            UpdateControls();
            ShowFeedback(
                L"Path protected",
                L"The selected path is excluded now and on future scans.",
                InfoBarSeverity::Success);
        });
        menu.Items().Append(protect);
        row.ContextFlyout(menu);
        return row;
    }

    Brush CleanPage::TierBrush(::Orbit::Core::CleanTier tier) const
    {
        wchar_t const* resource = L"OrbitTierRiskyBrush";
        if (tier == ::Orbit::Core::CleanTier::Safe)
        {
            resource = L"OrbitTierSafeBrush";
        }
        else if (tier == ::Orbit::Core::CleanTier::Review)
        {
            resource = L"OrbitTierReviewBrush";
        }
        return Application::Current().Resources().Lookup(
            box_value(resource)).as<Brush>();
    }

    hstring CleanPage::TierLabel(::Orbit::Core::CleanTier tier)
    {
        switch (tier)
        {
        case ::Orbit::Core::CleanTier::Safe: return L"Safe";
        case ::Orbit::Core::CleanTier::Review: return L"Review";
        default: return L"Risky";
        }
    }

    hstring CleanPage::FormatAge(FILETIME const& lastWrite)
    {
        ULARGE_INTEGER written{};
        written.LowPart = lastWrite.dwLowDateTime;
        written.HighPart = lastWrite.dwHighDateTime;
        if (written.QuadPart == 0) return L"Unknown";

        FILETIME nowFileTime{};
        ::GetSystemTimeAsFileTime(&nowFileTime);
        ULARGE_INTEGER now{};
        now.LowPart = nowFileTime.dwLowDateTime;
        now.HighPart = nowFileTime.dwHighDateTime;
        if (now.QuadPart <= written.QuadPart) return L"Just now";

        uint64_t seconds = (now.QuadPart - written.QuadPart) / 10'000'000ULL;
        if (seconds < 3600) return hstring(std::to_wstring(seconds / 60) + L" min");
        if (seconds < 86400) return hstring(std::to_wstring(seconds / 3600) + L" hr");
        return hstring(std::to_wstring(seconds / 86400) + L" days");
    }
}