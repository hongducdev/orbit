#pragma once

#if __has_include("Views/CleanPage.g.h")
#include "Views/CleanPage.g.h"
#else
#include "CleanPage.g.h"
#endif

#include "../ViewModels/CleanViewModel.h"

#include <array>
#include <memory>
#include <string>

namespace winrt::Orbit::implementation
{
    struct CleanPage : CleanPageT<CleanPage>
    {
        CleanPage();

        void ScanButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CancelButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SearchBox_TextChanged(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
        void RiskyToggle_Toggled(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void AddWhitelist_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CleanButton_Click(
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        static constexpr size_t kInitialVisibleFiles = 250;

        std::unique_ptr<::Orbit::ViewModels::CleanViewModel> m_viewModel;
        std::array<bool, static_cast<size_t>(::Orbit::Core::CleanCategoryId::Count)>
            m_expandedCategories{};
        std::array<size_t, static_cast<size_t>(::Orbit::Core::CleanCategoryId::Count)>
            m_visibleFileLimits{};
        bool m_hasScanned{ false };
        bool m_showRisky{ false };
        bool m_rendering{ false };
        std::wstring m_searchText;

        winrt::fire_and_forget StartScanAsync();
        winrt::fire_and_forget ConfirmAndCleanAsync();
        winrt::fire_and_forget ConfirmEmptyRecycleBinAsync();
        void RenderResults();
        void UpdateControls();
        void ShowFeedback(
            winrt::hstring const& title,
            winrt::hstring const& message,
            winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        winrt::Microsoft::UI::Xaml::Controls::Expander BuildCategory(
            size_t categoryIndex);
        winrt::Microsoft::UI::Xaml::Controls::StackPanel BuildCategoryContent(
            size_t categoryIndex);
        winrt::Microsoft::UI::Xaml::Controls::Grid BuildFileRow(
            size_t categoryIndex,
            size_t fileIndex);
        winrt::Microsoft::UI::Xaml::Media::Brush TierBrush(
            ::Orbit::Core::CleanTier tier) const;
        static winrt::hstring TierLabel(::Orbit::Core::CleanTier tier);
        static winrt::hstring FormatAge(FILETIME const& lastWrite);
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct CleanPage : CleanPageT<CleanPage, implementation::CleanPage>
    {
    };
}
