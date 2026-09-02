#include "pch.h"
#include "CleanPage.xaml.h"
#if __has_include("Views/CleanPage.g.cpp")
#include "Views/CleanPage.g.cpp"
#elif __has_include("CleanPage.g.cpp")
#include "CleanPage.g.cpp"
#endif

#include "../Helpers/AppSettings.h"
#include "../Core/OperationLog.h"

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

    template <typename T>
    T Resource(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(box_value(key)).as<T>();
    }

    wchar_t const* CategoryGlyph(::Orbit::Core::CleanCategoryId id)
    {
        switch (id)
        {
        case ::Orbit::Core::CleanCategoryId::TempUser: return L"\uE8B7";
        case ::Orbit::Core::CleanCategoryId::TempSystem: return L"\uE7F8";
        case ::Orbit::Core::CleanCategoryId::WinUpdateCache: return L"\uE895";
        case ::Orbit::Core::CleanCategoryId::DeliveryOptimization: return L"\uE753";
        case ::Orbit::Core::CleanCategoryId::ThumbCache: return L"\uE91B";
        case ::Orbit::Core::CleanCategoryId::ShaderCache: return L"\uE7FC";
        case ::Orbit::Core::CleanCategoryId::BrowserTemp: return L"\uE774";
        case ::Orbit::Core::CleanCategoryId::DevCaches: return L"\uE943";
        case ::Orbit::Core::CleanCategoryId::WerReports: return L"\uEA39";
        case ::Orbit::Core::CleanCategoryId::Prefetch: return L"\uE9E9";
        case ::Orbit::Core::CleanCategoryId::RecycleBin: return L"\uE74D";
        case ::Orbit::Core::CleanCategoryId::Count: break;
        }
        return L"\uE8B7";
    }

    wchar_t const* TierChipGlyph(::Orbit::Core::CleanTier tier)
    {
        switch (tier)
        {
        case ::Orbit::Core::CleanTier::Safe: return L"\uE73E";
        case ::Orbit::Core::CleanTier::Review: return L"\uE946";
        default: return L"\uE783";
        }
    }

    Border BuildCategoryIcon(
        ::Orbit::Core::CleanCategoryId id,
        Brush const& foreground,
        Brush const& background)
    {
        Border host;
        host.Style(Resource<Style>(L"CleanCategoryIconHostStyle"));
        host.Background(background);
        FontIcon icon;
        icon.Glyph(CategoryGlyph(id));
        icon.FontSize(16);
        icon.Foreground(foreground);
        icon.HorizontalAlignment(HorizontalAlignment::Center);
        icon.VerticalAlignment(VerticalAlignment::Center);
        host.Child(icon);
        return host;
    }

    Border BuildTierChip(
        hstring const& label,
        Brush const& foreground,
        Brush const& background,
        wchar_t const* glyph)
    {
        Border chip;
        chip.Style(Resource<Style>(L"CleanTierChipStyle"));
        chip.Background(background);

        StackPanel content;
        content.Orientation(Orientation::Horizontal);
        content.Spacing(4);
        content.HorizontalAlignment(HorizontalAlignment::Center);

        FontIcon icon;
        icon.Glyph(glyph);
        icon.FontSize(11);
        icon.Foreground(foreground);
        icon.VerticalAlignment(VerticalAlignment::Center);
        content.Children().Append(icon);

        TextBlock text;
        text.Text(label);
        text.FontSize(12);
        text.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        text.Foreground(foreground);
        text.VerticalAlignment(VerticalAlignment::Center);
        content.Children().Append(text);

        chip.Child(content);
        return chip;
    }

    StackPanel BuildImpactBlock(hstring const& sizeText, uint32_t fileCount)
    {
        StackPanel panel;
        panel.Spacing(0);
        panel.VerticalAlignment(VerticalAlignment::Center);
        panel.MinWidth(120);

        TextBlock size;
        size.Text(sizeText);
        size.Style(Resource<Style>(L"CleanImpactSizeStyle"));
        panel.Children().Append(size);

        TextBlock count;
        count.Text(hstring(
            fileCount == 1
                ? L"1 file"
                : std::to_wstring(fileCount) + L" files"));
        count.Style(Resource<Style>(L"OrbitBodySecondaryStyle"));
        count.FontSize(12);
        count.TextAlignment(TextAlignment::Right);
        count.HorizontalAlignment(HorizontalAlignment::Right);
        panel.Children().Append(count);
        return panel;
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

    void CleanPage::EmptyRecycleBinButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ConfirmEmptyRecycleBinAsync();
    }

    void CleanPage::ViewLogButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto logPath = ::Orbit::Core::OperationLog::LogFilePath();
        auto folderPath = logPath.parent_path();
        try
        {
            winrt::Windows::System::Launcher::LaunchFolderPathAsync(
                hstring(folderPath.wstring()));
        }
        catch (...)
        {
            ShowFeedback(
                L"Cannot open log folder",
                L"The operation history folder could not be opened.",
                InfoBarSeverity::Error);
        }
    }

    fire_and_forget CleanPage::StartScanAsync()
    {
        auto lifetime = get_strong();
        try
        {
            auto operation = m_viewModel->ScanAsync();
            UpdateControls();
            
            auto timer = DispatcherQueue().CreateTimer();
            timer.Interval(std::chrono::milliseconds(100));
            timer.IsRepeating(true);
            timer.Tick([weakThis = get_weak()](auto&&, auto&&) {
                if (auto strongThis = weakThis.get())
                {
                    strongThis->UpdateScanLiveUi();
                }
            });
            timer.Start();
            
            co_await operation;
            
            timer.Stop();
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

        // Start timer to update progress during deletion
        auto timer = DispatcherQueue().CreateTimer();
        timer.Interval(std::chrono::milliseconds(100));
        timer.IsRepeating(true);
        timer.Tick([weakThis = get_weak()](auto&&, auto&&) {
            if (auto strongThis = weakThis.get())
            {
                strongThis->UpdateControls();
            }
        });
        timer.Start();

        try
        {
            co_await operation;
        }
        catch (...)
        {
            timer.Stop();
            throw;
        }

        timer.Stop();
        RenderResults();
        UpdateControls();

        auto const& result = m_viewModel->lastDeleteResult;
        if (result.succeeded)
        {
            wchar_t message[256]{};
            swprintf_s(
                message,
                L"%zu items cleaned. %s Operation logged to history.jsonl.",
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

    fire_and_forget CleanPage::ConfirmEmptyRecycleBinAsync()
    {
        auto lifetime = get_strong();
        if (!m_viewModel->CanScan()) co_return;

        auto recycleBinBytes = ::Orbit::Platform::ShellOperations::GetRecycleBinSizeBytes();
        auto recycleBinCount = ::Orbit::Platform::ShellOperations::GetRecycleBinItemCount();
        
        if (recycleBinCount == 0)
        {
            ShowFeedback(
                L"Recycle Bin is empty",
                L"There are no items to delete.",
                InfoBarSeverity::Informational);
            co_return;
        }

        wchar_t summary[256]{};
        swprintf_s(
            summary,
            L"The Recycle Bin contains %u items (%s). Emptying permanently deletes them.",
            recycleBinCount,
            ::Orbit::Platform::ShellOperations::FormatBytes(recycleBinBytes).c_str());

        ContentDialog confirmation;
        confirmation.XamlRoot(XamlRoot());
        confirmation.Title(box_value(L"Empty Recycle Bin?"));
        confirmation.Content(box_value(hstring(summary)));
        confirmation.PrimaryButtonText(L"Empty Recycle Bin");
        confirmation.CloseButtonText(L"Cancel");
        confirmation.DefaultButton(ContentDialogButton::Close);
        if (co_await confirmation.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto operation = m_viewModel->EmptyRecycleBinAsync();
        UpdateControls();

        // Start timer to update progress during emptying
        auto timer = DispatcherQueue().CreateTimer();
        timer.Interval(std::chrono::milliseconds(100));
        timer.IsRepeating(true);
        timer.Tick([weakThis = get_weak()](auto&&, auto&&) {
            if (auto strongThis = weakThis.get())
            {
                strongThis->UpdateControls();
            }
        });
        timer.Start();

        try
        {
            co_await operation;
        }
        catch (...)
        {
            timer.Stop();
            throw;
        }

        timer.Stop();
        RenderResults();
        UpdateControls();

        auto const& result = m_viewModel->lastDeleteResult;
        if (result.succeeded)
        {
            ShowFeedback(
                L"Recycle Bin emptied",
                L"All recycled items were permanently deleted.",
                InfoBarSeverity::Success);
        }
        else
        {
            ShowFeedback(
                L"Could not empty Recycle Bin",
                result.error.empty()
                    ? hstring(L"Windows did not complete the operation.")
                    : hstring(result.error),
                InfoBarSeverity::Error);
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
        ScanProgress().Visibility(deleting ? Visibility::Visible : Visibility::Collapsed);

        if (scanning)
        {
            CategoryList().Visibility(Visibility::Collapsed);
            EmptyState().Visibility(Visibility::Visible);
        }
        UpdateScanLiveUi();

        if (deleting)
        {
            uint32_t completed = m_viewModel->cleanCompleted.load();
            uint32_t total = m_viewModel->cleanTotal.load();
            if (total > 0)
            {
                ScanProgress().IsIndeterminate(false);
                ScanProgress().Maximum(total);
                ScanProgress().Value(completed);
            }
            else
            {
                ScanProgress().IsIndeterminate(true);
            }
        }

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

        if (deleting)
        {
            uint32_t completed = m_viewModel->cleanCompleted.load();
            uint32_t total = m_viewModel->cleanTotal.load();
            if (total > 0)
            {
                wchar_t status[256]{};
                swprintf_s(
                    status,
                    permanent ? L"Permanently deleting %u of %u items…" : L"Moving %u of %u items to Recycle Bin…",
                    completed,
                    total);
                ScanStatusText().Text(status);
            }
            else
            {
                ScanStatusText().Text(hstring(m_viewModel->scanStatus));
            }
        }
        else
        {
            ScanStatusText().Text(hstring(m_viewModel->scanStatus));
        }
        
        // Enable Empty Recycle Bin button if not busy and Recycle Bin has items
        // Check directly via ShellOperations if not scanned yet, or via category data if scanned
        bool hasRecycleBinItems = false;
        if (m_hasScanned)
        {
            for (auto const& cat : m_viewModel->categories)
            {
                if (cat.id == ::Orbit::Core::CleanCategoryId::RecycleBin && cat.fileCount > 0)
                {
                    hasRecycleBinItems = true;
                    break;
                }
            }
        }
        else
        {
            hasRecycleBinItems = ::Orbit::Platform::ShellOperations::GetRecycleBinItemCount() > 0;
        }
        EmptyRecycleBinButton().IsEnabled(!busy && hasRecycleBinItems);
        
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
        
        // Show "View log" button for successful operations
        bool showLogButton = severity == InfoBarSeverity::Success &&
            (title == L"Clean complete" || title == L"Recycle Bin emptied");
        ViewLogButton().Visibility(
            showLogButton ? Visibility::Visible : Visibility::Collapsed);
        
        FeedbackBar().IsOpen(true);
    }

    void CleanPage::UpdateScanLiveUi()
    {
        bool scanning = m_viewModel->isScanning.load();
        ScanLivePanel().Visibility(scanning ? Visibility::Visible : Visibility::Collapsed);
        ScanStatusText().Text(hstring(m_viewModel->scanStatus));
        if (!scanning)
        {
            EmptyStateIcon().Glyph(L"\uE9D9");
            return;
        }

        uint32_t step = m_viewModel->scanCategoryIndex.load();
        uint32_t total = m_viewModel->scanCategoryTotal.load();
        uint32_t files = m_viewModel->scanFilesFound.load();
        if (total == 0)
        {
            total = 1;
        }

        std::wstring category;
        std::wstring path;
        m_viewModel->ReadScanLocation(category, path);

        EmptyStateIcon().Glyph(L"\uE721");
        EmptyStateTitle().Text(
            m_viewModel->cancelRequested.load()
                ? L"Cancelling scan…"
                : L"Scanning caches");
        EmptyStateMessage().Text(
            L"Dry run — nothing is deleted until you review and confirm.");

        ScanLiveCategory().Text(
            category.empty() ? hstring(L"Preparing…") : hstring(category));
        wchar_t stepText[32]{};
        swprintf_s(stepText, L"%u / %u", step, total);
        ScanLiveStep().Text(stepText);

        ScanCategoryProgress().Maximum(total);
        ScanCategoryProgress().IsIndeterminate(step == 0);
        if (step > 0)
        {
            ScanCategoryProgress().Value(step);
        }

        if (path.empty())
        {
            ScanLivePath().Text(L"Looking for cache folders…");
            ToolTipService::SetToolTip(ScanLivePath(), nullptr);
        }
        else
        {
            ScanLivePath().Text(hstring(path));
            ToolTipService::SetToolTip(ScanLivePath(), box_value(hstring(path)));
        }

        wchar_t counts[64]{};
        swprintf_s(
            counts,
            files == 1 ? L"1 file found" : L"%u files found",
            files);
        ScanLiveCounts().Text(counts);
    }

    Expander CleanPage::BuildCategory(size_t categoryIndex)
    {
        auto const& category = m_viewModel->categories[categoryIndex];
        Grid header;
        header.ColumnSpacing(12);
        header.MinHeight(56);
        header.HorizontalAlignment(HorizontalAlignment::Stretch);
        header.VerticalAlignment(VerticalAlignment::Center);
        AddColumn(header, GridLengthHelper::Auto());
        AddColumn(header, GridLengthHelper::Auto());
        AddColumn(header, GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        AddColumn(header, GridLengthHelper::FromPixels(96));
        AddColumn(header, GridLengthHelper::FromPixels(128));

        CheckBox selectAll;
        selectAll.IsThreeState(true);
        selectAll.MinWidth(32);
        selectAll.MinHeight(32);
        selectAll.VerticalAlignment(VerticalAlignment::Center);
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

        wchar_t const* chipBackgroundKey = L"OrbitTierRiskyChipBackgroundBrush";
        if (category.tier == ::Orbit::Core::CleanTier::Safe)
        {
            chipBackgroundKey = L"OrbitTierSafeChipBackgroundBrush";
        }
        else if (category.tier == ::Orbit::Core::CleanTier::Review)
        {
            chipBackgroundKey = L"OrbitTierReviewChipBackgroundBrush";
        }
        auto tierForeground = TierBrush(category.tier);
        auto chipBackground = Resource<Brush>(chipBackgroundKey);

        auto icon = BuildCategoryIcon(category.id, tierForeground, chipBackground);
        Grid::SetColumn(icon, 1);
        header.Children().Append(icon);

        StackPanel titlePanel;
        titlePanel.Spacing(2);
        titlePanel.VerticalAlignment(VerticalAlignment::Center);
        TextBlock title;
        title.Text(hstring(category.displayName));
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.FontSize(14);
        AutomationProperties::SetHeadingLevel(
            title,
            AutomationHeadingLevel::Level2);
        titlePanel.Children().Append(title);
        TextBlock description;
        description.Text(hstring(category.description));
        description.Style(Resource<winrt::Microsoft::UI::Xaml::Style>(L"OrbitBodySecondaryStyle"));
        description.FontSize(12);
        description.TextTrimming(TextTrimming::CharacterEllipsis);
        titlePanel.Children().Append(description);
        Grid::SetColumn(titlePanel, 2);
        header.Children().Append(titlePanel);

        auto chip = BuildTierChip(
            TierLabel(category.tier),
            tierForeground,
            chipBackground,
            TierChipGlyph(category.tier));
        AutomationProperties::SetName(
            chip,
            hstring(L"Safety: ") + TierLabel(category.tier));
        Grid::SetColumn(chip, 3);
        header.Children().Append(chip);

        auto impact = BuildImpactBlock(
            hstring(::Orbit::Platform::ShellOperations::FormatBytes(category.totalBytes)),
            category.fileCount);
        Grid::SetColumn(impact, 4);
        header.Children().Append(impact);

        Expander expander;
        expander.Style(Resource<winrt::Microsoft::UI::Xaml::Style>(L"CleanCategoryExpanderStyle"));
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