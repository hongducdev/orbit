#pragma once
#if __has_include("Views/CleanPage.g.h")
#include "Views/CleanPage.g.h"
#else
#include "CleanPage.g.h"
#endif

namespace winrt::Orbit::implementation
{
    struct CleanPage : CleanPageT<CleanPage>
    {
        CleanPage()
        {
            // Xaml objects should not call InitializeComponent during construction.
        }
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct CleanPage : CleanPageT<CleanPage, implementation::CleanPage>
    {
    };
}
