#include "HyphaReferenceHelp.h"

namespace hypha::reference_ui::help
{
namespace
{
std::vector<Region>* collecting = nullptr;  // メッセージスレッドが REF を描く間だけ
}

void note (juce::Rectangle<float> area, const juce::String& english)
{
    note (area.getSmallestIntegerContainer(), english);
}

void note (juce::Rectangle<int> area, const juce::String& english)
{
    if (collecting != nullptr && ! area.isEmpty() && english.isNotEmpty())
        collecting->push_back ({ area, english });
}

Collector::Collector (std::vector<Region>& regions) : previous (collecting)
{
    regions.clear();
    collecting = &regions;
}

Collector::~Collector()
{
    collecting = previous;
}

juce::String at (const std::vector<Region>& regions, juce::Point<int> point)
{
    for (auto region = regions.rbegin(); region != regions.rend(); ++region)
        if (region->area.contains (point))
            return region->text;
    return {};
}
}
