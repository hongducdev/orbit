#pragma once
#if __has_include("Views/AnalyzePage.g.h")
#include "Views/AnalyzePage.g.h"
#else
#include "AnalyzePage.g.h"
#endif

#include "../ViewModels/AnalyzeViewModel.h"

#include <memory>
#include <string>

namespace winrt::Orbit::implementation
{
    struct AnalyzePage : AnalyzePageT<AnalyzePage>
    {
        AnalyzePage();

        void DriveCombo_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void RemovableCheck_Changed(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ScanButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CancelButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void FilterBox_TextChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
        void SortCombo_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void GroupSmallCombo_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ShowFilesToggle_Toggled(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PathCrumbs_ItemClicked(
            winrt::Microsoft::UI::Xaml::Controls::BreadcrumbBar const&,
            winrt::Microsoft::UI::Xaml::Controls::BreadcrumbBarItemClickedEventArgs const&);
        void TreemapHost_SizeChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void DetailList_ItemClick(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        void RecycleButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void MoreButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OverviewList_ItemClick(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        void BackButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RefreshOverview_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        std::unique_ptr<::Orbit::ViewModels::AnalyzeViewModel> m_viewModel;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_filterTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_progressTimer{ nullptr };
        std::wstring m_pendingFilter;
        bool m_fillingDrives{ false };
        bool m_syncingDrive{ false };
        bool m_ready{ false };
        bool m_optionsOpen{ false };
        bool m_showingMap{ false };
        bool m_sidebarDirtySort{ false };
        std::wstring m_sidebarFolderPath;
        std::wstring m_scanOverride;

        void FillDrives();
        void FillOverview();
        void ShowMap(bool visible);
        void UpdateFreeSpace();
        void UpdateControls();
        void RefreshVisuals();
        void RefreshBreadcrumbs();
        void RefreshDetailList();
        void SyncDriveCombo(std::wstring const& path);
        winrt::fire_and_forget StartOverviewAsync();
        winrt::fire_and_forget StartScanAsync();
        winrt::fire_and_forget DrillIntoAsync(std::wstring path);
        winrt::fire_and_forget ConfirmDeleteAsync();
        void ShowContextMenu(std::wstring const& itemPath);
        void ShowFeedback(
            winrt::hstring const& title,
            winrt::hstring const& message,
            winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        std::wstring SelectedRoot();
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct AnalyzePage : AnalyzePageT<AnalyzePage, implementation::AnalyzePage>
    {
    };
}
