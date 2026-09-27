#include "HyphaAttackLanePainter.h"

#include <array>
#include <initializer_list>

#include "HyphaAttackStage.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// DRUM at 100% is read at a glance. Under the one-row HISTORY the selected hit's four values stand
// large in four cells, each led by its lane's colour mark and named by its code, with the delta sign
// when PRE is paired. All four use the one face in which every present value fits, so no cell looks
// louder than another. A withheld value shows its short reason.
namespace hypha::attack_lane_painter
{
namespace
{
using attack_lanes::index;
using attack_lanes::Lane;
using attack_lanes::Reason;
using typography::Composition;
using typography::TextRole;

juce::Rectangle<int> rectangleOf (attack_ui::Box box)
{
    return { box.x, box.y, box.width, box.height };
}

juce::String unitFor (Lane lane, bool delta)
{
    return lane == Lane::sharpness ? "acum" : lane == Lane::strength && ! delta ? "dBFS" : "dB";
}

juce::String cellText (const attack_lanes::Hit* hit, Lane lane, bool delta)
{
    if (hit == nullptr)
        return "--";
    const auto& cell = hit->cells[index (lane)];
    if (cell.reason == Reason::value)
        return valueText (lane, cell.value, delta, false);
    return reasonText (*hit, cell.reason).upToFirstOccurrenceOf (" ", false, false);
}
}

void paintGlance (juce::Graphics& g, const attack_ui::Layout& layout, const Frame& frame)
{
    const auto* hit = frame.selected;
    const bool delta = frame.model.delta;
    std::array<juce::String, attack_ui::laneCount> texts;
    std::array<juce::Rectangle<int>, attack_ui::laneCount> cells;
    for (const auto lane : attack_lanes::lanes)
    {
        cells[index (lane)] = rectangleOf (attack_ui::lineCell (layout, index (lane)));
        texts[index (lane)] = cellText (hit, lane, delta);
    }
    juce::Font valueFont;
    for (const auto composition : { Composition::facts, Composition::instrument,
                                    Composition::visualization })
    {
        valueFont = monoFont (frame.context, TextRole::primaryValue, composition);
        bool fits = true;
        for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
            fits = fits && tabularTextWidth (valueFont, texts[lane])
                               <= static_cast<float> (cells[lane].getWidth() - attack_ui::lineAccentWidth - 2);
        if (fits)
            break;
    }
    const auto labelHeight = text_style::requiredLineHeight (
        typography::resolve (frame.context, TextRole::metricLabel, Composition::visualization));
    for (const auto lane : attack_lanes::lanes)
    {
        auto cell = cells[index (lane)];
        const bool measured = hit != nullptr && hit->cells[index (lane)].reason == Reason::value;
        paintAccent (g, cell, colourFor (lane), measured ? 0.9f : 0.35f);
        cell.removeFromLeft (attack_ui::lineAccentWidth);
        auto head = cell.removeFromTop (labelHeight);
        g.setColour (colourFor (lane));
        drawFitting (g, { (delta ? hypha::delta() : juce::String()) + codeFor (lane) }, head,
                     frame.context, TextRole::metricLabel, juce::Justification::centredLeft,
                     attack_stage::labelTracking (frame.context));
        g.setColour (COL_TEXT_TERTIARY);
        drawFitting (g, { unitFor (lane, delta) }, head.withTrimmedRight (2), frame.context,
                     TextRole::unit, juce::Justification::centredRight);
        g.setColour (measured ? COL_OBSERVATORY_VALUE : COL_TEXT_TERTIARY);
        drawTabularText (g, valueFont, texts[index (lane)], cell.toFloat(),
                         juce::Justification::centredLeft);
    }
}
}
