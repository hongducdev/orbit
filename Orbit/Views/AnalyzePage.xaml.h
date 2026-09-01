#pragma once
#if __has_include("Views/AnalyzePage.g.h")
#include "Views/AnalyzePage.g.h"
#else
#include "AnalyzePage.g.h"
#endif

namespace winrt::Orbit::implementation
{
    struct AnalyzePage : AnalyzePageT<AnalyzePage>
    {
        AnalyzePage() {}
    };
}

namespace winrt::Orbit::factory_implementation
{
    struct AnalyzePage : AnalyzePageT<AnalyzePage, implementation::AnalyzePage>
    {
    };
}
