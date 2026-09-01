#include "pch.h"
#include "SettingsPage.xaml.h"
#if __has_include("Views/SettingsPage.g.cpp")
#include "Views/SettingsPage.g.cpp"
#elif __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif
#include "Helpers/AppSettings.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::Orbit::implementation
{
    SettingsPage::SettingsPage()
    {
        // Keep ctor empty per pch guidance; Loaded handles init.
        Loaded([this](auto&&, auto&&) {
            ApplySettingsToUI();
            // Show package version
            try
            {
                auto version = Windows::ApplicationModel::Package::Current().Id().Version();
                wchar_t buf[64]{};
                swprintf_s(buf, L"Version %u.%u.%u.%u", version.Major, version.Minor, version.Build, version.Revision);
                VersionText().Text(buf);
            }
            catch (...) {}
            m_loading = false;
        });
    }

    void SettingsPage::ApplySettingsToUI()
    {
        // Planet animation
        PlanetAnimationToggle().IsOn(::Orbit::Helpers::AppSettings::PlanetAnimationEnabled());

        // Theme
        auto theme = ::Orbit::Helpers::AppSettings::Theme();
        int idx = 0;
        if (theme == L"light") idx = 1;
        else if (theme == L"dark") idx = 2;
        ThemeCombo().SelectedIndex(idx);

        // Delete mode
        auto dm = ::Orbit::Helpers::AppSettings::DeleteMode();
        DeleteModeCombo().SelectedIndex(dm == L"permanent" ? 1 : 0);
    }

    void SettingsPage::PlanetAnimationToggle_Toggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_loading) return;
        auto toggle = sender.as<ToggleSwitch>();
        ::Orbit::Helpers::AppSettings::PlanetAnimationEnabled(toggle.IsOn());
    }

    void SettingsPage::ThemeCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_loading) return;
        auto item = ThemeCombo().SelectedItem().try_as<ComboBoxItem>();
        if (!item) return;
        auto tag = unbox_value<hstring>(item.Tag());
        ::Orbit::Helpers::AppSettings::Theme(tag);
        this->RequestedTheme(::Orbit::Helpers::AppSettings::ToElementTheme(tag));
    }

    void SettingsPage::DeleteModeCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_loading) return;
        auto item = DeleteModeCombo().SelectedItem().try_as<ComboBoxItem>();
        if (!item) return;
        auto tag = unbox_value<hstring>(item.Tag());
        ::Orbit::Helpers::AppSettings::DeleteMode(tag);
    }
}
