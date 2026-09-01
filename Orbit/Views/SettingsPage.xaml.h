#pragma once
#if __has_include("Views/SettingsPage.g.h")
#include "Views/SettingsPage.g.h"
#else
#include "SettingsPage.g.h"
#endif

namespace winrt::Orbit::implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage>
    {
        SettingsPage();

        void PlanetAnimationToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ThemeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void DeleteModeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);

    private:
        bool m_loading{ true };
        void ApplySettingsToUI();
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
    {
    };
}
