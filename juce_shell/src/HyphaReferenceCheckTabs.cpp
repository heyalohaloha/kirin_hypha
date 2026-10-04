#include "HyphaReferenceCheckTabs.h"

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha::reference_ui
{
namespace
{
}

CheckTabs::CheckTabs()
{
    setComponentID ("reference-check-tabs");
    setTitle ("Checks");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void CheckTabs::setTabs (std::vector<Tab> next, const juce::String& selected, presentation::Context nextContext)
{
    items = std::move (next);
    selectedId = selected;
    context = nextContext;
    repaint();
}

std::vector<juce::Rectangle<int>> CheckTabs::layoutTabs() const
{
    // 各タブは文字の幅に合わせ、入りきらなければ等分に縮める（あふれる名前は省略記号で示す）。
    auto area = getLocalBounds();  // 2026-10-04：「1 / 5」は出さない（どのタブかは下線で分かる）
    const auto font = labelFont (context, typography::TextRole::body, typography::Composition::information);
    std::vector<int> widths;
    int total = 0;
    for (const auto& tab : items)
    {
        widths.push_back (juce::roundToInt (text_style::shownWidth (font, tab.label)) + 22);
        total += widths.back();
    }
    std::vector<juce::Rectangle<int>> result;
    const auto available = area.getWidth();
    for (size_t index = 0; index < items.size(); ++index)
    {
        const auto width = total <= available ? widths[index] : available / static_cast<int> (items.size());
        result.push_back (area.removeFromLeft (width));
    }
    return result;
}

juce::Rectangle<int> CheckTabs::tabBounds (size_t index) const
{
    const auto bounds = layoutTabs();
    return index < bounds.size() ? bounds[index] : juce::Rectangle<int> {};
}

void CheckTabs::paint (juce::Graphics& g)
{
    const auto bounds = layoutTabs();
    g.setColour (COL_MUTED.withAlpha (0.22f));
    g.fillRect (getLocalBounds().removeFromBottom (1));
    size_t selectedIndex = items.size();
    for (size_t index = 0; index < items.size(); ++index)
    {
        const bool on = items[index].id == selectedId;
        if (on) selectedIndex = index;
        auto cell = bounds[index];
        g.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
        g.setColour (on ? COL_OBSERVATORY_VALUE : COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, items[index].label, cell.reduced (10, 0).withTrimmedBottom (3),
                                    juce::Justification::centred);
        if (on)
        {
            g.setColour (COL_FLORA_BR.withAlpha (0.92f));
            g.fillRect (cell.removeFromBottom (2).reduced (6, 0));
        }
    }
}

void CheckTabs::mouseDown (const juce::MouseEvent& event)
{
    const auto bounds = layoutTabs();
    for (size_t index = 0; index < bounds.size(); ++index)
        if (bounds[index].contains (event.getPosition()))
        {
            if (items[index].id != selectedId && onChoose) onChoose (items[index].id);
            return;
        }
}
}
