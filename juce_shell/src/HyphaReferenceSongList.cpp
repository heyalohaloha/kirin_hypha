#include "HyphaReferenceSongList.h"

#include "HyphaReferenceGainText.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <array>

namespace hypha::reference_ui
{
namespace
{
constexpr int barWidth = 8;  // 入りきらない時の送りの帯

juce::String number (double value)
{
    if (! std::isfinite (value)) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
    return juce::String (value, 1);
}

// 番号・曲名・LUFS-I・MATCH・状態の列。見出しと行で同じ区切り。
std::array<juce::Rectangle<int>, 5> columns (juce::Rectangle<int> row, int width)
{
    auto rest = row;
    const auto index = rest.removeFromLeft (22);
    const auto state = rest.removeFromRight (juce::jmax (64, width / 6));
    const auto gain = rest.removeFromRight (juce::jmax (52, width / 7));
    const auto lufs = rest.removeFromRight (juce::jmax (52, width / 7));
    return { index, rest.reduced (4, 0), lufs, gain, state };
}
}

SongList::SongList()
{
    setComponentID ("reference-song-list");
    setTitle ("Reference songs");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SongList::setRows (std::vector<Row> next, presentation::Context nextContext)
{
    items = std::move (next);
    context = nextContext;
    // 選んだ曲が替わったら、その行が見える所まで送る（キーで曲を替えても見失わない）。
    for (size_t index = 0; index < items.size(); ++index)
        if (items[index].selected && items[index].id != shownSelection)
        {
            shownSelection = items[index].id;
            const auto row = static_cast<int> (index), visible = juce::jmax (1, layout().visible);
            if (row < firstRow) firstRow = row;
            else if (row >= firstRow + visible) firstRow = row - visible + 1;
        }
    scrollTo (firstRow);
}

void SongList::setOverlay (bool all)
{
    if (overlayAll == all) return;
    overlayAll = all;
    placeToggles();
    repaint();
}

int SongList::rowHeight() const noexcept
{
    return context.density == observatory::Density::inspection ? 34 : 26;
}

SongList::Layout SongList::layout() const noexcept
{
    Layout result;
    result.content = getLocalBounds().reduced (8, 6);
    auto content = result.content;
    result.header = content.removeFromTop (18);
    if (liveShown()) result.live = content.removeFromTop (rowHeight());
    result.visible = juce::jmax (0, content.getHeight() / rowHeight());
    if (static_cast<int> (items.size()) > result.visible && result.visible > 0)
        result.bar = content.removeFromRight (barWidth).withHeight (result.visible * rowHeight());
    result.rows = content;
    return result;
}

void SongList::scrollTo (int row)
{
    const auto shown = layout();
    const auto last = juce::jmax (0, static_cast<int> (items.size()) - shown.visible);
    const auto next = juce::jlimit (0, last, row);
    firstRow = next;
    placeToggles();
    repaint();
}

int SongList::rowAt (juce::Point<int> point) const noexcept
{
    const auto shown = layout();
    if (! shown.rows.contains (point)) return -1;
    const auto offset = (point.getY() - shown.rows.getY()) / rowHeight();
    const auto index = firstRow + offset;
    return offset < shown.visible && index < static_cast<int> (items.size()) ? index : -1;
}

void SongList::placeToggles()
{
    const auto shown = layout();
    while (toggles.size() < static_cast<size_t> (juce::jmax (0, shown.visible)))
    {
        auto toggle = std::make_unique<NumberToggle>();
        // The box reads its row when pressed: a press can redraw the list (and move what each box shows).
        toggle->onClick = [this, slot = static_cast<int> (toggles.size())]
        {
            const auto index = static_cast<size_t> (firstRow + slot);
            if (index >= items.size() || ! onToggle) return;
            const auto id = items[index].id;
            onToggle (id);
        };
        toggle->addMouseListener (this, false);
        addChildComponent (*toggle);
        toggles.push_back (std::move (toggle));
    }
    for (size_t slot = 0; slot < toggles.size(); ++slot)
    {
        auto& toggle = *toggles[slot];
        const auto index = firstRow + static_cast<int> (slot);
        const bool used = overlayAll && static_cast<int> (slot) < shown.visible && index < static_cast<int> (items.size());
        toggle.setVisible (used);
        if (! used) continue;
        const auto& row = items[static_cast<size_t> (index)];
        const auto bounds = shown.rows.withY (shown.rows.getY() + static_cast<int> (slot) * rowHeight()).withHeight (rowHeight());
        toggle.setBounds (columns (bounds, shown.rows.getWidth())[0]);
        toggle.show (index + 1, row.shown, row.colour, context);
    }
}

void SongList::setLive (double loudness, int seconds)
{
    const bool same = (std::isnan (loudness) && std::isnan (liveLoudness)) || std::abs (loudness - liveLoudness) < 0.05;
    if (same && seconds == liveSeconds) return;
    const bool moved = std::isfinite (loudness) != liveShown();
    liveLoudness = loudness;
    liveSeconds = seconds;
    if (moved) scrollTo (firstRow);  // the A row takes a row's room
    repaint();
}

void SongList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    surface_material::paintPanel (g, area, 0.72f);
    const auto shown = layout();
    const auto width = shown.rows.getWidth();
    const auto header = columns (shown.header.withWidth (width), width);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    text_style::drawEllipsized (g, "SONG", header[1], juce::Justification::centredLeft);
    text_style::drawEllipsized (g, "LUFS-I", header[2], juce::Justification::centredRight);
    text_style::drawEllipsized (g, "MATCH", header[3], juce::Justification::centredRight);
    // 列ごとに見出しを付ける（状態の列に見出しが無く、MATCH が READY の見出しに見えていた。2026-10-04）。
    text_style::drawEllipsized (g, "STATE", header[4], juce::Justification::centredRight);
    if (liveShown())
    {
        // A の行（金）：B の曲の LUFS-I と並べて読む。押せない（選ぶのは B の曲だけ）。
        const auto cells = columns (shown.live.withWidth (width), width);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        g.setColour (COL_FLORA_BR);
        text_style::drawEllipsized (g, "A", cells[0], juce::Justification::centred);
        g.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
        text_style::drawEllipsized (g, "A LAST " + juce::String (liveSeconds) + " S", cells[1], juce::Justification::centredLeft);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, number (liveLoudness), cells[2], juce::Justification::centredRight);
        g.setColour (COL_FLORA_BR.withAlpha (0.25f));
        g.drawHorizontalLine (shown.live.getBottom() - 1, static_cast<float> (shown.content.getX()), static_cast<float> (shown.content.getRight()));
    }
    auto content = shown.rows;
    for (auto index = static_cast<size_t> (firstRow); index < items.size() && content.getHeight() >= rowHeight(); ++index)
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
        const auto cells = columns (bounds, width);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        g.setColour (COL_MUTED);
        if (! overlayAll) text_style::drawEllipsized (g, juce::String (static_cast<int> (index) + 1), cells[0], juce::Justification::centred);
        g.setColour (row.selected ? COL_SPECTRUM_DELTA_BR : COL_OBSERVATORY_VALUE);
        g.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
        text_style::drawEllipsizedKeepingVersion (g, row.title, cells[1], juce::Justification::centredLeft);
        g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, number (row.lufsI), cells[2], juce::Justification::centredRight);
        // 鳴っている曲は実際の gain、ほかは押したときの gain（薄く）。
        g.setColour (row.playing ? COL_SPECTRUM_DELTA : COL_MUTED);
        if (row.preparation != "NOT FOUND" && row.preparation != "ANALYSIS FAILED" && std::isfinite (row.gainDb)) text_style::drawEllipsized (g, gainText (row.gainDb), cells[3], juce::Justification::centredRight);
        const bool missing = row.preparation == "NOT FOUND" || row.preparation == "ANALYSIS FAILED";  // Kirin OS がファイルを確かめられない（待っても進まない）
        g.setColour (row.playing ? COL_SPECTRUM_DELTA : row.switching || (row.preparing && ! missing) ? COL_FLORA : COL_MUTED);
        // 目標は測った値だけ（音は無い）。
        text_style::drawEllipsized (g, row.target ? juce::String ("DATA") : std::isfinite (row.lowerAOfferDb) && row.lowerAOfferDb < 0.0
                                       ? "A " + gainText (row.lowerAOfferDb) : missing ? row.preparation : row.playing ? "PLAYING" : row.switching ? "SWITCHING" : ! row.preparing ? (row.loaded ? "READY" : "OS READY")
                                       : row.preparation.isNotEmpty() ? row.preparation : "PREPARING",
                                    cells[4], juce::Justification::centredRight);
    }
    if (! shown.bar.isEmpty())
    {
        // 送りの帯：全体の中で見えている所。
        const auto total = static_cast<float> (items.size());
        const auto track = shown.bar.toFloat().reduced (2.0f, 0.0f);
        g.setColour (COL_MUTED.withAlpha (0.22f));
        g.fillRoundedRectangle (track, 2.0f);
        const auto top = track.getY() + track.getHeight() * static_cast<float> (firstRow) / total;
        const auto height = track.getHeight() * static_cast<float> (shown.visible) / total;
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.75f));
        g.fillRoundedRectangle (track.withY (top).withHeight (juce::jmax (10.0f, height)), 2.0f);
    }
}

void SongList::mouseDown (const juce::MouseEvent& event)
{
    if (event.eventComponent != this) return;  // a number box takes its own press
    const auto shown = layout();
    if (shown.bar.contains (event.getPosition())) { mouseDrag (event); return; }
    // 見出し（上の 18 px）・A の行・余白・描いていない行（はみ出した行）を押しても選ばない（paint と同じ区切り）。
    const auto index = rowAt (event.getPosition());
    if (index < 0) return;
    const auto row = items[static_cast<size_t> (index)];
    const auto cells = columns (shown.rows, shown.rows.getWidth());
    if (overlayAll && event.getPosition().getX() < cells[0].getRight())
    {
        if (onToggle) onToggle (row.id);
        return;
    }
    if (! row.selected && onChoose) onChoose (row.id);
}

void SongList::mouseDrag (const juce::MouseEvent& event)
{
    const auto shown = layout();
    const auto at = event.getEventRelativeTo (this).getPosition();
    if (event.eventComponent != this || shown.bar.isEmpty() || ! shown.bar.expanded (6, 0).contains (event.getMouseDownPosition())) return;
    const auto fraction = static_cast<double> (at.getY() - shown.bar.getY()) / juce::jmax (1, shown.bar.getHeight());
    scrollTo (juce::roundToInt (fraction * static_cast<double> (items.size()) - shown.visible * 0.5));
}

void SongList::mouseMove (const juce::MouseEvent& event)
{
    const auto index = rowAt (event.getEventRelativeTo (this).getPosition());
    const auto next = index >= 0 ? items[static_cast<size_t> (index)].id : juce::String();
    if (next == pointed) return;
    pointed = next;
    if (onPoint) onPoint (pointed);
}

void SongList::mouseExit (const juce::MouseEvent& event)
{
    if (getLocalBounds().contains (event.getEventRelativeTo (this).getPosition())) return;  // onto a number box
    if (pointed.isEmpty()) return;
    pointed.clear();
    if (onPoint) onPoint ({});
}

void SongList::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& details)
{
    if (event.eventComponent != this) return;  // a number box passes its wheel to the list
    if (layout().bar.isEmpty()) { juce::Component::mouseWheelMove (event, details); return; }
    wheel -= details.deltaY * 8.0;
    const auto rows = static_cast<int> (wheel);
    if (rows == 0) return;
    wheel -= rows;
    scrollTo (firstRow + rows);
}
}
