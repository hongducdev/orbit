#include "pch.h"
#include "AnalyzePage.xaml.h"
#if __has_include("Views/AnalyzePage.g.cpp")
#include "Views/AnalyzePage.g.cpp"
#elif __has_include("AnalyzePage.g.cpp")
#include "AnalyzePage.g.cpp"
#endif

#include "AnalyzeBarRow.h"
#include "AnalyzeTreemapControl.h"
#include "../Core/TreemapLayout.h"
#include "../Helpers/AppSettings.h"
#include "../Platform/PathHelpers.h"
#include "../Platform/ShellOperations.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.UI.h>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::ApplicationModel::DataTransfer::Clipboard;
using winrt::Windows::ApplicationModel::DataTransfer::DataPackage;

namespace winrt::Orbit::implementation
{
    AnalyzePage::AnalyzePage() :
        m_viewModel(std::make_unique<::Orbit::ViewModels::AnalyzeViewModel>())
    {
        Unloaded([this](auto&&, auto&&) {
            m_viewModel->CancelOverview();
            m_viewModel->CancelScan();
            if (m_progressTimer) m_progressTimer.Stop();
        });
        Loaded([this](auto&&, auto&&) {
            m_filterTimer = DispatcherQueue().CreateTimer();
            m_filterTimer.Interval(std::chrono::milliseconds(200));
            m_filterTimer.IsRepeating(false);
            m_filterTimer.Tick([this](auto&&, auto&&) {
                m_viewModel->SetFilter(m_pendingFilter);
                RefreshVisuals();
            });
            m_progressTimer = DispatcherQueue().CreateTimer();
            m_progressTimer.Interval(std::chrono::milliseconds(180));
            m_progressTimer.IsRepeating(true);
            m_progressTimer.Tick([this](auto&&, auto&&) {
                if (m_showingMap && m_viewModel->previewReady.load()) RefreshVisuals();
                else if (!m_showingMap && m_viewModel->isOverviewSizing.load())
                {
                    FillOverview();
                    UpdateControls();
                }
                else UpdateControls();
            });
            FillDrives();
            if (m_showingMap)
            {
                if (auto* current = m_viewModel->CurrentNode())
                {
                    SyncDriveCombo(current->path);
                }
                else
                {
                    ShowMap(false);
                    m_viewModel->ResetOverview(SelectedRoot());
                    FillOverview();
                }
            }
            else
            {
                ShowMap(false);
                m_viewModel->ResetOverview(SelectedRoot());
                FillOverview();
            }
            m_ready = true;
#ifdef _DEBUG
            if (!::Orbit::Core::TreemapLayout::SelfTest())
            {
                ShowFeedback(
                    L"Treemap self-test failed",
                    L"Layout produced overlapping or non-proportional rectangles.",
                    InfoBarSeverity::Warning);
            }
#endif
        });
    }

    void AnalyzePage::FillDrives()
    {
        auto previous = m_ready ? SelectedRoot() : std::wstring();
        m_fillingDrives = true;
        DriveCombo().Items().Clear();
        auto drives = ::Orbit::ViewModels::AnalyzeViewModel::EnumerateDrives(
            RemovableCheck().IsChecked() && RemovableCheck().IsChecked().Value());
        int select = 0;
        for (size_t i = 0; i < drives.size(); ++i)
        {
            auto const& drive = drives[i];
            ComboBoxItem item;
            std::wstring text = drive.root + L"  " + drive.label + L"  (" +
                ::Orbit::Platform::ShellOperations::FormatBytes(drive.freeBytes) + L" free)";
            item.Content(box_value(hstring(text)));
            item.Tag(box_value(hstring(drive.root)));
            DriveCombo().Items().Append(item);
            if (!previous.empty() && _wcsicmp(drive.root.c_str(), previous.c_str()) == 0)
            {
                select = static_cast<int>(i);
            }
            else if (previous.empty() && _wcsicmp(drive.root.c_str(), L"C:\\") == 0)
            {
                select = static_cast<int>(i);
            }
        }
        if (!drives.empty()) DriveCombo().SelectedIndex(select);
        m_fillingDrives = false;
        UpdateFreeSpace();
        if (m_ready)
        {
            auto now = SelectedRoot();
            if (previous.empty() || _wcsicmp(previous.c_str(), now.c_str()) != 0)
            {
                ShowMap(false);
                m_sidebarFolderPath.clear();
                m_viewModel->ResetOverview(now);
                FillOverview();
                PathCrumbs().ItemsSource(nullptr);
            }
        }
    }

    void AnalyzePage::UpdateFreeSpace()
    {
        auto root = SelectedRoot();
        ULARGE_INTEGER freeBytes{};
        if (::GetDiskFreeSpaceExW(root.c_str(), nullptr, nullptr, &freeBytes))
        {
            FreeSpaceText().Text(hstring(
                root + L"  ·  " +
                ::Orbit::Platform::ShellOperations::FormatBytes(freeBytes.QuadPart) +
                L" free"));
        }
    }

    std::wstring AnalyzePage::SelectedRoot()
    {
        auto item = DriveCombo().SelectedItem().try_as<ComboBoxItem>();
        if (!item) return L"C:\\";
        auto tag = item.Tag();
        if (!tag) return L"C:\\";
        return std::wstring(unbox_value_or<hstring>(tag, L"C:\\"));
    }

    void AnalyzePage::SyncDriveCombo(std::wstring const& path)
    {
        if (path.size() < 2 || path[1] != L':') return;
        std::wstring root{ path[0], L':', L'\\' };
        if (_wcsicmp(root.c_str(), SelectedRoot().c_str()) == 0)
        {
            UpdateFreeSpace();
            return;
        }
        m_syncingDrive = true;
        for (uint32_t i = 0; i < DriveCombo().Items().Size(); ++i)
        {
            auto item = DriveCombo().Items().GetAt(i).try_as<ComboBoxItem>();
            if (!item) continue;
            auto tag = std::wstring(unbox_value_or<hstring>(item.Tag(), L""));
            if (_wcsicmp(tag.c_str(), root.c_str()) == 0)
            {
                DriveCombo().SelectedIndex(static_cast<int>(i));
                break;
            }
        }
        m_syncingDrive = false;
        UpdateFreeSpace();
        m_viewModel->ResetOverview(SelectedRoot());
    }

    void AnalyzePage::DriveCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_fillingDrives || m_syncingDrive || !m_ready) return;
        m_viewModel->CancelOverview();
        m_viewModel->CancelScan();
        ShowMap(false);
        m_sidebarFolderPath.clear();
        UpdateFreeSpace();
        m_viewModel->ResetOverview(SelectedRoot());
        FillOverview();
        PathCrumbs().ItemsSource(nullptr);
    }
    void AnalyzePage::RemovableCheck_Changed(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_fillingDrives) FillDrives();
    }
    void AnalyzePage::ScanButton_Click(IInspectable const&, RoutedEventArgs const&) { StartScanAsync(); }
    void AnalyzePage::CancelButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_viewModel->CancelOverview();
        m_viewModel->CancelScan();
    }

    void AnalyzePage::FilterBox_TextChanged(
        IInspectable const&,
        AutoSuggestBoxTextChangedEventArgs const&)
    {
        m_pendingFilter = std::wstring(FilterBox().Text());
        if (!m_filterTimer) return;
        m_filterTimer.Stop();
        m_filterTimer.Start();
    }

    void AnalyzePage::SortCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        m_viewModel->sortByName = SortCombo().SelectedIndex() == 1;
        RefreshVisuals();
    }
    void AnalyzePage::GroupSmallCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        uint64_t const thresholds[] = { 0, 1024ull * 1024ull, 10ull * 1024ull * 1024ull, 50ull * 1024ull * 1024ull };
        int index = GroupSmallCombo().SelectedIndex();
        if (index < 0 || index > 3) index = 1;
        m_viewModel->groupSmallBytes = thresholds[index];
        RefreshVisuals();
    }
    void AnalyzePage::ShowFilesToggle_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        m_viewModel->showFiles = ShowFilesToggle().IsChecked() && ShowFilesToggle().IsChecked().Value();
        RefreshVisuals();
    }
    void AnalyzePage::BackButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_viewModel->CancelScan();
        m_showingMap = false;
        ShowMap(false);
        UpdateControls();
        FillOverview();
    }

    void AnalyzePage::RefreshOverview_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StartOverviewAsync();
    }
    void AnalyzePage::PathCrumbs_ItemClicked(
        BreadcrumbBar const&,
        BreadcrumbBarItemClickedEventArgs const& args)
    {
        m_viewModel->NavigateToCrumb(static_cast<size_t>(args.Index()));
        RefreshVisuals();
    }
    void AnalyzePage::TreemapHost_SizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        RefreshVisuals();
    }
    void AnalyzePage::DetailList_ItemClick(IInspectable const&, ItemClickEventArgs const& args)
    {
        auto grid = args.ClickedItem().try_as<Grid>();
        if (!grid) return;
        auto path = unbox_value_or<hstring>(grid.Tag(), L"");
        if (path.empty()) return;
        DrillIntoAsync(std::wstring(path));
    }
    void AnalyzePage::RecycleButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ConfirmDeleteAsync();
    }

    void AnalyzePage::MoreButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_optionsOpen = !m_optionsOpen;
        OptionsRow().Visibility(m_optionsOpen ? Visibility::Visible : Visibility::Collapsed);
        MoreButton().Content(box_value(m_optionsOpen ? L"Hide options" : L"Options"));
    }

    void AnalyzePage::OverviewList_ItemClick(IInspectable const&, ItemClickEventArgs const& args)
    {
        auto element = args.ClickedItem().try_as<FrameworkElement>();
        if (!element) return;
        auto path = unbox_value_or<hstring>(element.Tag(), L"");
        if (path.empty()) return;
        m_scanOverride = std::wstring(path);
        StartScanAsync();
    }

    void AnalyzePage::FillOverview()
    {
        OverviewList().Items().Clear();
        auto locations = m_viewModel->OverviewSnapshot();
        if (locations.empty())
        {
            locations = ::Orbit::ViewModels::AnalyzeViewModel::OverviewLocations(SelectedRoot());
        }
        uint64_t folderTotal = 0;
        uint64_t diskCapacity = 0;
        ULARGE_INTEGER totalBytes{};
        if (::GetDiskFreeSpaceExW(SelectedRoot().c_str(), nullptr, &totalBytes, nullptr))
        {
            diskCapacity = totalBytes.QuadPart;
        }
        for (auto const& row : locations)
        {
            if (row.name != L"This disk") folderTotal += row.size;
        }
        int index = 1;
        for (auto const& row : locations)
        {
            uint64_t denom = row.name == L"This disk" ? diskCapacity : folderTotal;
            OverviewList().Items().Append(
                ::Orbit::Views::MakeUsageBarRow(row.path, row.name, row.size, denom, index++));
        }
        OverviewStatus().Text(m_viewModel->overviewStatus.empty()
            ? L"Select a location to explore"
            : hstring(m_viewModel->overviewStatus));
    }

    void AnalyzePage::ShowMap(bool visible)
    {
        m_showingMap = visible;
        OverviewCard().Visibility(visible ? Visibility::Collapsed : Visibility::Visible);
        ExplorerHost().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        BackButton().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        PathCrumbs().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }

    void AnalyzePage::UpdateControls()
    {
        bool scanning = m_viewModel->isScanning.load();
        bool measuring = m_viewModel->isOverviewSizing.load();
        bool busy = scanning || measuring;
        ScanButton().IsEnabled(!busy);
        CancelButton().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        ScanProgress().Visibility((!m_showingMap && busy) ? Visibility::Visible : Visibility::Collapsed);
        uint32_t totalDirs = m_viewModel->dirsTotal.load();
        uint32_t doneDirs = m_viewModel->dirsDone.load();
        if (m_showingMap && scanning && totalDirs > 0)
        {
            MapProgress().Visibility(Visibility::Visible);
            MapProgress().IsIndeterminate(false);
            MapProgress().Maximum(static_cast<double>(totalDirs));
            MapProgress().Value(static_cast<double>(doneDirs));
        }
        else if (m_showingMap && scanning)
        {
            MapProgress().Visibility(Visibility::Visible);
            MapProgress().IsIndeterminate(true);
        }
        else
        {
            MapProgress().Visibility(Visibility::Collapsed);
        }
        RefreshOverviewButton().IsEnabled(!busy);
        wchar_t status[256]{};
        swprintf_s(
            status,
            L"%s — %u files (%s)",
            m_viewModel->scanStatus.c_str(),
            m_viewModel->filesFound.load(),
            ::Orbit::Platform::ShellOperations::FormatBytes(m_viewModel->bytesFound.load()).c_str());
        StatusText().Text(status);
        uint32_t selected = m_viewModel->SelectedCount();
        SelectionText().Text(selected == 0
            ? L"No items selected"
            : hstring(std::to_wstring(selected) + L" selected (" +
                ::Orbit::Platform::ShellOperations::FormatBytes(m_viewModel->SelectedBytes()) + L")"));
        RecycleButton().IsEnabled(selected > 0 && !scanning);
        if (!m_showingMap)
        {
            OverviewStatus().Text(m_viewModel->overviewStatus.empty()
                ? L"Select a location to explore"
                : hstring(m_viewModel->overviewStatus));
        }
    }

    void AnalyzePage::RefreshVisuals()
    {
        if (!m_ready) return;
        if (!m_showingMap)
        {
            UpdateControls();
            return;
        }
        double width = TreemapCanvas().ActualWidth();
        double height = TreemapCanvas().ActualHeight();
        if (width < 16.0 || height < 16.0)
        {
            UpdateControls();
            return;
        }
        auto items = m_viewModel->Layout(width, height);
        uint64_t parent = m_viewModel->CurrentNode() ? m_viewModel->CurrentNode()->size : 0;
        auto selected = std::unordered_set<std::wstring>();
        for (auto const& path : m_viewModel->SelectedPaths()) selected.insert(path);
        ::Orbit::Views::TreemapRenderCallbacks callbacks;
        callbacks.onTapped = [this](std::wstring const& path, bool additive) {
            m_viewModel->ToggleSelect(path, additive);
            RefreshVisuals();
        };
        callbacks.onDrill = [this](std::wstring const& path) { DrillIntoAsync(path); };
        callbacks.onRightTapped = [this](std::wstring const& itemPath) {
            ShowContextMenu(itemPath);
        };
        ::Orbit::Views::AnalyzeTreemapControl::Render(
            TreemapCanvas(), items, parent, selected, callbacks);
        RefreshBreadcrumbs();
        RefreshDetailList();
        if (auto* current = m_viewModel->CurrentNode())
        {
            SidebarSummary().Text(hstring(
                std::to_wstring(current->children.size()) + L" items  ·  " +
                ::Orbit::Platform::ShellOperations::FormatBytes(current->size)));
        }
        UpdateControls();
    }

    void AnalyzePage::RefreshBreadcrumbs()
    {
        auto labels = m_viewModel->BreadcrumbLabels();
        auto items = single_threaded_observable_vector<IInspectable>();
        for (auto const& label : labels) items.Append(box_value(hstring(label)));
        PathCrumbs().ItemsSource(items);
    }

    void AnalyzePage::RefreshDetailList()
    {
        bool scanning = m_viewModel->isScanning.load();
        std::wstring folder;
        if (auto* current = m_viewModel->CurrentNode()) folder = current->path;
        auto rows = m_viewModel->VisibleListItems(scanning);
        uint64_t total = 0;
        for (auto const& row : rows) total += row.size;
        if (rows.size() > 80) rows.resize(80);

        bool newFolder = folder != m_sidebarFolderPath;
        m_sidebarFolderPath = folder;
        if (newFolder)
        {
            DetailList().Items().Clear();
            m_sidebarDirtySort = scanning;
        }
        else if (!scanning && m_sidebarDirtySort)
        {
            DetailList().Items().Clear();
            m_sidebarDirtySort = false;
            rows = m_viewModel->VisibleListItems(false);
            total = 0;
            for (auto const& row : rows) total += row.size;
            if (rows.size() > 80) rows.resize(80);
        }
        else if (scanning)
        {
            m_sidebarDirtySort = true;
        }

        auto items = DetailList().Items();
        if (items.Size() == 0)
        {
            for (auto const& row : rows)
            {
                items.Append(::Orbit::Views::MakeSidebarRow(row.path, row.name, row.size, total));
            }
            return;
        }

        std::unordered_map<std::wstring, uint32_t> indexByPath;
        for (uint32_t i = 0; i < items.Size(); ++i)
        {
            auto grid = items.GetAt(i).try_as<Grid>();
            if (!grid) continue;
            auto path = std::wstring(unbox_value_or<hstring>(grid.Tag(), L""));
            if (!path.empty()) indexByPath.emplace(std::move(path), i);
        }
        for (auto const& row : rows)
        {
            auto found = indexByPath.find(row.path);
            if (found != indexByPath.end())
            {
                auto grid = items.GetAt(found->second).try_as<Grid>();
                if (grid) ::Orbit::Views::UpdateSidebarRow(grid, row.size, total);
            }
            else
            {
                items.Append(::Orbit::Views::MakeSidebarRow(row.path, row.name, row.size, total));
            }
        }
    }

    fire_and_forget AnalyzePage::StartOverviewAsync()
    {
        auto lifetime = get_strong();
        if (!m_progressTimer) co_return;
        m_progressTimer.Start();
        FillOverview();
        co_await m_viewModel->RefreshOverviewAsync(SelectedRoot());
        if (!m_viewModel->isScanning.load()) m_progressTimer.Stop();
        FillOverview();
        UpdateControls();
    }

    fire_and_forget AnalyzePage::StartScanAsync()
    {
        auto lifetime = get_strong();
        auto root = m_scanOverride.empty() ? SelectedRoot() : m_scanOverride;
        m_scanOverride.clear();
        SyncDriveCombo(root);
        ShowMap(true);
        TreemapCanvas().Children().Clear();
        m_progressTimer.Start();
        UpdateControls();
        co_await m_viewModel->ScanAsync(root);
        m_progressTimer.Stop();
        if (m_viewModel->truncated)
        {
            ShowFeedback(L"Scan truncated", L"Stopped at 1 million nodes. Drill into folders to expand them.", InfoBarSeverity::Informational);
        }
        RefreshVisuals();
    }

    fire_and_forget AnalyzePage::DrillIntoAsync(std::wstring path)
    {
        auto lifetime = get_strong();
        if (!m_viewModel->DrillIn(path))
        {
            m_viewModel->ToggleSelect(path, true);
            RefreshVisuals();
            co_return;
        }
        SyncDriveCombo(path);
        RefreshVisuals();
        if (m_viewModel->NeedsExpand())
        {
            m_progressTimer.Start();
            co_await m_viewModel->ExpandCurrentAsync();
            m_progressTimer.Stop();
            RefreshVisuals();
        }
    }

    fire_and_forget AnalyzePage::ConfirmDeleteAsync()
    {
        auto lifetime = get_strong();
        uint32_t count = m_viewModel->SelectedCount();
        uint64_t bytes = m_viewModel->SelectedBytes();
        bool permanent = ::Orbit::Helpers::AppSettings::DeleteMode() == L"permanent";
        bool extraConfirm = count > 100 || bytes > (1ull << 30);
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(permanent ? L"Permanently delete selected items?" : L"Move selected items to Recycle Bin?"));
        wchar_t summary[256]{};
        swprintf_s(
            summary,
            L"%u items (%s).%s",
            count,
            ::Orbit::Platform::ShellOperations::FormatBytes(bytes).c_str(),
            extraConfirm ? L" This is a large selection." : L"");
        dialog.Content(box_value(hstring(summary)));
        dialog.PrimaryButtonText(permanent ? L"Delete permanently" : L"Move to Recycle Bin");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        co_await m_viewModel->DeleteSelectedAsync(permanent);
        auto const& result = m_viewModel->lastDeleteResult;
        ShowFeedback(
            result.succeeded ? L"Items moved" : L"Delete did not fully complete",
            hstring(result.error.empty()
                ? (std::to_wstring(result.completedCount) + L" items processed")
                : result.error),
            result.succeeded ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
        RefreshVisuals();
    }

    void AnalyzePage::ShowContextMenu(std::wstring const& itemPath)
    {
        MenuFlyout flyout;
        auto add = [&](hstring const& label, std::function<void()> action) {
            MenuFlyoutItem menuItem;
            menuItem.Text(label);
            menuItem.Click([action](auto&&, auto&&) { action(); });
            flyout.Items().Append(menuItem);
        };
        if (m_viewModel->CanDeletePath(itemPath))
        {
            add(L"Move to Recycle Bin", [this, itemPath] {
                m_viewModel->ClearSelection();
                m_viewModel->ToggleSelect(itemPath, true);
                ConfirmDeleteAsync();
            });
        }
        add(L"Reveal in Explorer", [itemPath] {
            ::Orbit::Platform::PathHelpers::RevealInExplorer(itemPath);
        });
        add(L"Copy path", [itemPath] {
            DataPackage pack;
            pack.SetText(hstring(itemPath));
            Clipboard::SetContent(pack);
        });
        add(L"Whitelist this path", [this, itemPath] {
            bool ok = m_viewModel->AddWhitelist(itemPath);
            ShowFeedback(
                ok ? L"Path protected" : L"Could not add whitelist",
                hstring(itemPath),
                ok ? InfoBarSeverity::Success : InfoBarSeverity::Warning);
        });
        flyout.ShowAt(TreemapCanvas());
    }

    void AnalyzePage::ShowFeedback(
        hstring const& title,
        hstring const& message,
        InfoBarSeverity severity)
    {
        FeedbackBar().Title(title);
        FeedbackBar().Message(message);
        FeedbackBar().Severity(severity);
        FeedbackBar().IsOpen(true);
    }
}
