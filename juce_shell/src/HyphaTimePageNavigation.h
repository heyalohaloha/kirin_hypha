#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaAnalysisNavigation.h"
#include "HyphaObservatoryView.h"

namespace hypha
{
// TIME exposes four readings without creating four analyzer owners. Compact keeps one cycling
// control; Observatory makes the same pages directly addressable. FREQ remains a first-level
// domain and never appears in this component.
class TimePageNavigation final : public juce::Component
{
public:
    using Page = analysis_navigation::Page;

    TimePageNavigation();

    std::function<void (Page)> onPageChange;

    void setPage (Page);
    Page page() const noexcept { return selectedPage; }
    void setDirect (bool);
    bool isDirect() const noexcept { return direct; }
    int visibleDirectTabCount() const noexcept;
    void resized() override;

private:
    void choose (Page);
    void updateControls();

    Page selectedPage = Page::meters;
    bool direct = false;
    juce::TextButton compactCycle;
    observatory::Button historyButton { "HISTORY", true };
    observatory::Button attackButton { "ATTACK", true };
    observatory::Button sharpButton { "SHARP", true };
    observatory::Button liveButton { "LIVE", true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimePageNavigation)
};
}
