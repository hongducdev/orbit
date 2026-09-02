#pragma once
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "../Core/TreemapLayout.h"

namespace Orbit::Views
{

struct TreemapRenderCallbacks
{
    std::function<void(std::wstring const& itemPath, bool additive)> onTapped;
    std::function<void(std::wstring const& itemPath)> onDrill;
    std::function<void(std::wstring const& itemPath)> onRightTapped;
};

class AnalyzeTreemapControl
{
public:
    static void Render(
        winrt::Microsoft::UI::Xaml::Controls::Canvas const& canvas,
        std::vector<Core::TreemapItem> const& items,
        uint64_t parentSize,
        std::unordered_set<std::wstring> const& selected,
        TreemapRenderCallbacks const& callbacks);
};

} // namespace Orbit::Views
