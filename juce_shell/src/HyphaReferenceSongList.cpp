#include "HyphaReferenceSongList.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <array>

namespace hypha::reference_ui
{
namespace
{
juce::String number (double value, bool signedValue)
{
    if (! std::isfinite (value)) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
    return (signedValue && value >= 0.0 ? "+" : "") + juce::String (value, 1);
}
}

SongList::SongList()
{
    setComponentID ("reference-song-list");
    setTitle ("B songs");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SongList::setRows (std::vector<Row> next, presentation::Context nextContext)
{
    items = std::move (next);
    context = nextContext;
    repaint();
}

int SongList::rowHeight() const noexcept
{
    return context.density == observatory::Density::inspection ? 34 : 26;
}

void SongList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    surface_material::paintPanel (g, area, 0.72f);
    auto content = getLocalBounds().reduced (8, 6);
    const auto columns = [&content] (juce::Rectangle<int> row) {
        auto rest = row;
        const auto index = rest.removeFromLeft (22);
        const auto state = rest.removeFromRight (juce::jmax (64, content.getWidth() / 6));
        const auto gain = rest.removeFromRight (juce::jmax (52, content.getWidth() / 7));
        const auto lufs = rest.removeFromRight (juce::jmax (52, content.getWidth() / 7));
        return std::array<juce::Rectangle<int>, 5> { index, rest.reduced (4, 0), lufs, gain, state };
    };
    const auto header = columns (content.removeFromTop (18));
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    text_style::drawEllipsized (g, "SONG", header[1], juce::Justification::centredLeft);
    text_style::drawEllipsized (g, "LUFS-I", header[2], juce::Justification::centredRight);
    text_style::drawEllipsized (g, "MATCH", header[3], juce::Justification::centredRight);
    for (size_t index = 0; index < items.size() && content.getHeight() >= rowHeight(); ++index)
    {
        const auto& row = items[index];
        auto bounds = content.removeFromTop (rowHeight());
        if (row.selected)
        {
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.08f));
            g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.85f));
            g.fillRect (bounds.removeFromLeft (2).toFloat());
        }
        const auto cells = columns (bounds);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        g.setColour (COL_MUTED);
        text_style::drawEllipsized (g, juce::String (static_cast<int> (index) + 1), cells[0], juce::Justification::centred);
        g.setColour (row.selected ? COL_SPECTRUM_DELTA_BR : COL_OBSERVATORY_VALUE);
        g.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
        text_style::drawEllipsized (g, row.title, cells[1], juce::Justification::centredLeft);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, number (row.lufsI, false), cells[2], juce::Justification::centredRight);
        g.setColour (COL_SPECTRUM_DELTA);
        if (row.playing) text_style::drawEllipsized (g, number (row.gainDb, true), cells[3], juce::Justification::centredRight);
        g.setColour (row.playing ? COL_SPECTRUM_DELTA : row.preparing ? COL_FLORA : COL_MUTED);
        text_style::drawEllipsized (g, row.playing ? "PLAYING" : row.preparing ? "PREPARING" : "READY", cells[4],
                                    juce::Justification::centredRight);
    }
}

void SongList::mouseDown (const juce::MouseEvent& event)
{
    // 見出し（上の 18 px）・余白・描いていない行（はみ出した行）を押しても選ばない（paint と同じ区切り）。
    auto content = getLocalBounds().reduced (8, 6);
    content.removeFromTop (18);
    if (! content.contains (event.getPosition())) return;
    const auto index = (event.getPosition().getY() - content.getY()) / rowHeight();
    if (index >= content.getHeight() / rowHeight() || index >= static_cast<int> (items.size())) return;
    const auto& row = items[static_cast<size_t> (index)];
    if (! row.selected && onChoose) onChoose (row.id);
}
}
