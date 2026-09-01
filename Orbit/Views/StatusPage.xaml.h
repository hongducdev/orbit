#pragma once
#if __has_include("Views/StatusPage.g.h")
#include "Views/StatusPage.g.h"
#else
#include "StatusPage.g.h"
#endif

namespace winrt::Orbit::implementation
{
    struct StatusPage : StatusPageT<StatusPage>
    {
        StatusPage() {}
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct StatusPage : StatusPageT<StatusPage, implementation::StatusPage>
    {
    };
}
