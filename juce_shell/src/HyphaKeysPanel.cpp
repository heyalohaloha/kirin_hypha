#include "HyphaKeysPanel.h"

#include <cmath>

#include "HyphaComparisonSurfaceMaterial.h"
#include "HyphaTheme.h"

namespace hypha
{
namespace
{
int columnWidth (const juce::Font& font, const std::vector<shortcuts::ListRow>& rows,
                 juce::String shortcuts::ListRow::* column, text_style::LabelPolicy policy)
{
    float widest = 0.0f;
    for (const auto& row : rows) widest = juce::jmax (widest, text_style::shownWidth (font, row.*column, policy));
    return juce::roundToInt (std::ceil (widest)) + 2;
}
}

int KeysPanel::List::rowHeight() const noexcept
{
    return context.density == observatory::Density::compact ? 18 : 22;
}

void KeysPanel::List::paint (juce::Graphics& g)
{
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    const auto font = g.getCurrentFont();
    const auto fixed = text_style::LabelPolicy::fixed;
    const auto keysWidth = columnWidth (font, rows, &shortcuts::ListRow::keys, fixed);
    const auto whereWidth = columnWidth (font, rows, &shortcuts::ListRow::where, fixed);
    // The labels are the controls' own words: as fixed as REVEAL and END on their buttons.
    const auto word = [&] (const juce::String& text, juce::Rectangle<int> area, juce::Justification justification)
    {
        text_style::draw (g, text, area, context, typography::TextRole::unit, justification, 1,
                          typography::Composition::information, fixed);
    };
    int y = 0;
    for (const auto& row : rows)
    {
        auto line = juce::Rectangle<int> (0, y, getWidth(), rowHeight()).reduced (2, 0);
        y += rowHeight();
        g.setColour (row.usable ? COL_TEXT_SECONDARY : COL_MUTED);
        word (row.where, line.removeFromRight (whereWidth), juce::Justification::centredRight);
        line.removeFromRight (10);
        g.setColour (row.usable ? COL_FLORA_BR : COL_MUTED);
        word (row.keys, line.removeFromRight (keysWidth), juce::Justification::centredLeft);
        line.removeFromRight (10);
        g.setColour (row.usable ? COL_NORMAL : COL_MUTED);
        word (row.label, line, juce::Justification::centredLeft);
    }
}

KeysPanel::KeysPanel()
{
    setComponentID ("keys-panel");
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    title.setComponentID ("keys-title");
    title.setText ("KEYS", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, COL_NORMAL);
    title.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (title);
    closeButton.setComponentID ("keys-close");
    closeButton.setTitle ("CLOSE");
    closeButton.setTooltip ("Close KEYS");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    viewport.setComponentID ("keys-list");
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.setWantsKeyboardFocus (true);
    viewport.setViewedComponent (&list, false);
    addAndMakeVisible (viewport);
}

void KeysPanel::setRows (std::vector<shortcuts::ListRow> next)
{
    if (next == shownRows) return;
    shownRows = std::move (next);
    resized();
    list.repaint();
}

void KeysPanel::focusList()
{
    if (viewport.isShowing()) viewport.grabKeyboardFocus();
}

void KeysPanel::parentSizeChanged()
{
    if (auto* parent = getParentComponent()) setBounds (parent->getLocalBounds());
}

void KeysPanel::resized()
{
    const auto context = presentation::forEditor (getWidth(), getHeight());
    const bool compact = getWidth() < 450;
    list.setContext (context);
    closeButton.setPresentationContext (context);
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    auto area = getLocalBounds().reduced (compact ? 16 : 28, compact ? 14 : 24);
    auto header = area.removeFromTop (compact ? 24 : 34);
    closeButton.setBounds (header.removeFromRight (compact ? 72 : 96).reduced (0, 1));
    title.setBounds (header);
    area.removeFromTop (compact ? 4 : 10);
    viewport.setBounds (area);
    const auto height = static_cast<int> (shownRows.size()) * list.rowHeight();
    const auto scrolls = height > area.getHeight();
    list.setSize (area.getWidth() - (scrolls ? viewport.getScrollBarThickness() + 4 : 0), height);
    list.repaint();
}

void KeysPanel::paint (juce::Graphics& g)
{
    comparison_surface::paintScreen (g, *this);
}
}
