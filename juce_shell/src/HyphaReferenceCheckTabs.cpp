#include "HyphaReferenceCheckTabs.h"

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace hypha::reference_ui
{
namespace
{
// 1 段の中で入りきらなければ、短い名前は全部出し、残りを長い名前で等分する（等分だけだと短い名前まで切れた）。
std::vector<int> fitRow (const std::vector<int>& natural, int available)
{
    std::vector<int> result (natural.size());
    std::vector<size_t> order (natural.size());
    std::iota (order.begin(), order.end(), size_t { 0 });
    std::stable_sort (order.begin(), order.end(), [&natural] (size_t a, size_t b) { return natural[a] < natural[b]; });
    auto remaining = available;
    auto left = static_cast<int> (natural.size());
    for (const auto index : order)
    {
        result[index] = std::min (natural[index], remaining / std::max (1, left));
        remaining -= result[index];
        --left;
    }
    return result;
}

constexpr int minimumRowHeight = 20;
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

std::vector<int> CheckTabs::naturalWidths() const
{
    const auto font = labelFont (context, typography::TextRole::body, typography::Composition::information);
    std::vector<int> widths;
    for (const auto& tab : items)
        widths.push_back (juce::roundToInt (std::ceil (text_style::shownWidth (font, tab.label))) + 22);
    return widths;
}

int CheckTabs::rowsFor (int width) const
{
    const auto widths = naturalWidths();
    return items.size() > 1 && std::accumulate (widths.begin(), widths.end(), 0) > width ? 2 : 1;
}

std::vector<juce::Rectangle<int>> CheckTabs::layoutTabs() const
{
    // 各タブは文字の幅に合わせる（2026-10-04：「1 / 5」は出さない。どのタブかは下線で分かる）。
    auto area = getLocalBounds();
    const auto widths = naturalWidths();
    const auto total = std::accumulate (widths.begin(), widths.end(), 0);
    std::vector<juce::Rectangle<int>> result (items.size());
    const auto place = [&] (size_t from, size_t to, juce::Rectangle<int> row)
    {
        const std::vector<int> natural (widths.begin() + static_cast<std::ptrdiff_t> (from), widths.begin() + static_cast<std::ptrdiff_t> (to));
        const auto fitted = fitRow (natural, row.getWidth());
        for (size_t index = from; index < to; ++index) result[index] = row.removeFromLeft (fitted[index - from]);
    };
    if (total <= area.getWidth() || items.size() < 2 || area.getHeight() < 2 * minimumRowHeight)
    {
        place (0, items.size(), area);
        return result;
    }
    // 2 段：セットの順のまま、2 段の幅がなるべくそろう所で分ける。
    size_t split = 1;
    auto best = std::numeric_limits<int>::max(), before = 0;
    for (size_t index = 1; index < items.size(); ++index)
    {
        before += widths[index - 1];
        if (const auto wider = std::max (before, total - before); wider < best) { best = wider; split = index; }
    }
    place (0, split, area.removeFromTop (area.getHeight() / 2));
    place (split, items.size(), area);
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
            if (onChoose) onChoose (items[index].id);
            return;
        }
}
}
