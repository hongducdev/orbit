#pragma once
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace Orbit::Helpers
{
    // Thin wrapper over ApplicationData::Current().LocalSettings() for Phase 2 toggles.
    // Keys are intentionally stable — future phases must not rename them.
    struct AppSettings
    {
        static constexpr wchar_t kPlanetAnimation[] = L"PlanetAnimationEnabled";
        static constexpr wchar_t kDeleteMode[] = L"DeleteMode"; // "recycle" | "permanent"
        static constexpr wchar_t kTheme[] = L"Theme";           // "default" | "light" | "dark"

        static winrt::Windows::Storage::ApplicationDataContainer LocalSettings()
        {
            return winrt::Windows::Storage::ApplicationData::Current().LocalSettings();
        }

        static bool PlanetAnimationEnabled()
        {
            auto values = LocalSettings().Values();
            auto v = values.TryLookup(kPlanetAnimation);
            if (v) return winrt::unbox_value<bool>(v);
            return true; // default ON
        }

        static void PlanetAnimationEnabled(bool value)
        {
            LocalSettings().Values().Insert(kPlanetAnimation, winrt::box_value(value));
        }

        static winrt::hstring DeleteMode()
        {
            auto values = LocalSettings().Values();
            auto v = values.TryLookup(kDeleteMode);
            if (v) return winrt::unbox_value<winrt::hstring>(v);
            return L"recycle";
        }

        static void DeleteMode(winrt::hstring const& mode)
        {
            LocalSettings().Values().Insert(kDeleteMode, winrt::box_value(mode));
        }

        static winrt::hstring Theme()
        {
            auto values = LocalSettings().Values();
            auto v = values.TryLookup(kTheme);
            if (v) return winrt::unbox_value<winrt::hstring>(v);
            return L"default";
        }

        static void Theme(winrt::hstring const& theme)
        {
            LocalSettings().Values().Insert(kTheme, winrt::box_value(theme));
        }

        static winrt::Microsoft::UI::Xaml::ElementTheme ToElementTheme(winrt::hstring const& theme)
        {
            if (theme == L"light") return winrt::Microsoft::UI::Xaml::ElementTheme::Light;
            if (theme == L"dark") return winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
            return winrt::Microsoft::UI::Xaml::ElementTheme::Default;
        }
    };
}
