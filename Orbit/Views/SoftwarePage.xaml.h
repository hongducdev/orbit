#pragma once
#if __has_include("Views/SoftwarePage.g.h")
#include "Views/SoftwarePage.g.h"
#else
#include "SoftwarePage.g.h"
#endif

namespace winrt::Orbit::implementation
{
    struct SoftwarePage : SoftwarePageT<SoftwarePage>
    {
        SoftwarePage() {}
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct SoftwarePage : SoftwarePageT<SoftwarePage, implementation::SoftwarePage>
    {
    };
}
