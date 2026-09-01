#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "Views/CleanPage.xaml.h"
#include "Views/AnalyzePage.xaml.h"
#include "Views/SoftwarePage.xaml.h"
#include "Views/OptimizePage.xaml.h"
#include "Views/StatusPage.xaml.h"
#include "Views/SettingsPage.xaml.h"
#include "Helpers/AppSettings.h"
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml::Interop;


namespace winrt::Orbit::implementation
{
    void MainWindow::NavView_Loaded(IInspectable const&, RoutedEventArgs const&)
    {
        // Extend into titlebar
        try
        {
            ExtendsContentIntoTitleBar(true);
        }
        catch (...) {}

        // Window sizing: ensure at least 1080x720 via AppWindow if available
        try
        {
            auto hwnd = GetWindowHandle();
            if (hwnd)
            {
                RECT bounds{};
                if (::GetWindowRect(hwnd, &bounds))
                {
                    int32_t width = (std::max)(bounds.right - bounds.left, 1080L);
                    int32_t height = (std::max)(bounds.bottom - bounds.top, 720L);
                    ::SetWindowPos(
                        hwnd,
                        nullptr,
                        0,
                        0,
                        width,
                        height,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
        }
        catch (...) {}

        ApplyMicaBackdrop();
        ApplyThemeFromSettings();
        SelectInitialItem();
    }

    void MainWindow::ApplyMicaBackdrop()
    {
        try
        {
            if (winrt::Microsoft::UI::Composition::SystemBackdrops::MicaController::IsSupported())
            {
                SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop{});
            }
            else
            {
                SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::DesktopAcrylicBackdrop{});
            }
        }
        catch (...) {}
    }

    void MainWindow::ApplyThemeFromSettings()
    {
        try
        {
            auto theme = ::Orbit::Helpers::AppSettings::Theme();
            auto elemTheme = ::Orbit::Helpers::AppSettings::ToElementTheme(theme);
            if (auto nv = NavView())
            {
                nv.RequestedTheme(elemTheme);
            }
            if (auto frame = ContentFrame())
            {
                frame.RequestedTheme(elemTheme);
            }
        }
        catch (...) {}
    }

    void MainWindow::SelectInitialItem()
    {
        try
        {
            auto items = NavView().MenuItems();
            if (items.Size() > 0)
            {
                NavView().SelectedItem(items.GetAt(0));
                NavigateTo(L"Clean");
            }
        }
        catch (...) {}
    }

    void MainWindow::NavigateTo(hstring const& tag)
    {
        try
        {
            auto frame = ContentFrame();
            if (!frame) return;

            if (tag == L"Clean")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::CleanPage>());
            }
            else if (tag == L"Analyze")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::AnalyzePage>());
            }
            else if (tag == L"Software")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::SoftwarePage>());
            }
            else if (tag == L"Optimize")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::OptimizePage>());
            }
            else if (tag == L"Status")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::StatusPage>());
            }
            else if (tag == L"Settings")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::SettingsPage>());
            }
            else if (tag == L"Doctor")
            {
                frame.Navigate(winrt::xaml_typename<winrt::Orbit::SettingsPage>());
            }
        }
        catch (...) {}
    }

    void MainWindow::NavView_SelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
    {
        auto selected = args.SelectedItem().try_as<NavigationViewItem>();
        if (!selected) return;
        auto tag = unbox_value<hstring>(selected.Tag());
        NavigateTo(tag);
    }

    void MainWindow::NavView_BackRequested(NavigationView const&, NavigationViewBackRequestedEventArgs const&)
    {
        try
        {
            auto frame = ContentFrame();
            if (frame.CanGoBack()) frame.GoBack();
        }
        catch (...) {}
    }

    void MainWindow::Shortcut_Invoked(KeyboardAccelerator const& sender, KeyboardAcceleratorInvokedEventArgs const& args)
    {
        auto key = sender.Key();
        hstring tag{};
        if (key == Windows::System::VirtualKey::Number1) tag = L"Clean";
        else if (key == Windows::System::VirtualKey::Number2) tag = L"Analyze";
        else if (key == Windows::System::VirtualKey::Number3) tag = L"Software";
        else if (key == Windows::System::VirtualKey::Number4) tag = L"Optimize";
        else if (key == Windows::System::VirtualKey::Number5) tag = L"Status";
        else return;

        try
        {
            auto items = NavView().MenuItems();
            for (uint32_t i = 0; i < items.Size(); ++i)
            {
                if (auto nvi = items.GetAt(i).try_as<NavigationViewItem>())
                {
                    if (unbox_value<hstring>(nvi.Tag()) == tag)
                    {
                        NavView().SelectedItem(nvi);
                        break;
                    }
                }
            }
        }
        catch (...) {}
        NavigateTo(tag);
        args.Handled(true);
    }

    void MainWindow::ShortcutSettings_Invoked(KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const& args)
    {
        try
        {
            auto footer = NavView().FooterMenuItems();
            for (uint32_t i = 0; i < footer.Size(); ++i)
            {
                if (auto nvi = footer.GetAt(i).try_as<NavigationViewItem>())
                {
                    if (unbox_value<hstring>(nvi.Tag()) == L"Settings")
                    {
                        NavView().SelectedItem(nvi);
                        break;
                    }
                }
            }
        }
        catch (...) {}
        NavigateTo(L"Settings");
        args.Handled(true);
    }

    HWND MainWindow::GetWindowHandle()
    {
        try
        {
            auto windowNative = this->try_as<IWindowNative>();
            HWND hwnd{ nullptr };
            if (windowNative) windowNative->get_WindowHandle(&hwnd);
            return hwnd;
        }
        catch (...) { return nullptr; }
    }
}
