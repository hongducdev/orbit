#pragma once
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace Orbit::ViewModels
{
    // Lightweight INotifyPropertyChanged helper for C++/WinRT ViewModels.
    // Usage: struct MyViewModel : ViewModelBase<MyViewModel> { ... RaisePropertyChanged(L"Foo"); }
    template <typename D>
    struct ViewModelBase : winrt::implements<D, winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged>
    {
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

        winrt::event_token PropertyChanged(winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        {
            return m_propertyChanged.add(handler);
        }

        void PropertyChanged(winrt::event_token const& token) noexcept
        {
            m_propertyChanged.remove(token);
        }

    protected:
        void RaisePropertyChanged(winrt::hstring const& propertyName)
        {
            m_propertyChanged(*static_cast<D*>(this), winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
        }

        // Helper: set field and raise if changed
        template <typename T>
        bool SetProperty(T& storage, T const& value, winrt::hstring const& propertyName)
        {
            if (storage == value) return false;
            storage = value;
            RaisePropertyChanged(propertyName);
            return true;
        }
    };
}
