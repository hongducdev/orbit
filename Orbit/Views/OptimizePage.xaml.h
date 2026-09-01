#pragma once
#if __has_include("Views/OptimizePage.g.h")
#include "Views/OptimizePage.g.h"
#else
#include "OptimizePage.g.h"
#endif

namespace winrt::Orbit::implementation
{
    struct OptimizePage : OptimizePageT<OptimizePage>
    {
        OptimizePage() {}
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct OptimizePage : OptimizePageT<OptimizePage, implementation::OptimizePage>
    {
    };
}
