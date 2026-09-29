#include "HyphaAttackComponent.h"

#include <limits>

#include "HyphaAttackBandPainter.h"
#include "HyphaAttackUiContract.h"

namespace hypha
{
AttackComponent::AttackComponent()
{
    setTitle ("DRUM Attack");
    setDescription ("Drum transient facts for TRACK/STEM; not a 2MIX onset detector.");
    setWantsKeyboardFocus (true);
}

// HISTORY, the axis and every lane share one plot column; any point in it selects by time. The
// band panes show one hit rather than six seconds, so with them only the axis and lanes select.
bool AttackComponent::selectsAt (const attack_ui::Layout& shape, juce::Point<int> point) const noexcept
{
    const auto history = rectangleOf (attack_ui::historyPlot (shape));
    if (history.isEmpty())
        return false;
    const auto top = bandPanes (shape) ? shape.axis.y : shape.history.y;
    const auto bottom = shape.arrangement == attack_ui::Arrangement::lanes ? shape.lanes.back().bottom()
                      : shape.arrangement == attack_ui::Arrangement::glance ? shape.history.bottom()
                                                                            : shape.axis.bottom();
    return point.x >= history.getX() && point.x < history.getRight()
        && point.y >= top && point.y < bottom;
}

void AttackComponent::mouseDown (const juce::MouseEvent& event)
{
    if (isShowing()) grabKeyboardFocus();
    const auto shape = layout();
    // 100% has no VIEW button or band chips (both are chosen at 125% and above); its HOLD / LOCK
    // caption, top right in HISTORY, returns to LIVE as NOW does in the axis row at the larger sizes.
    if (shape.arrangement == attack_ui::Arrangement::glance)
    {
        const auto history = rectangleOf (attack_ui::historyPlot (shape));
        if (! followLatest && history.withTrimmedLeft (history.getWidth() - 48).withHeight (18)
                                  .contains (event.getPosition()))
        {
            followLatest = true;
            selectBoundaryEvent (true);
            refreshBandEnvelope();
            repaint();
            return;
        }
    }
    else if (event.y < attack_ui::titleRowHeight (presentationContext)
             && event.x > getWidth() - viewControlWidth())
    {
        overlayMode = ! overlayMode;
        repaint();
        return;
    }
    for (std::size_t choice = 0; choice < attack_band::choiceCount; ++choice)
        if (rectangleOf (attack_band::chipCell (shape, presentationContext, choice))
                .contains (event.getPosition()))
        {
            setBand (static_cast<std::uint8_t> (choice));
            return;
        }
    const auto axis = rectangleOf (attack_ui::axisPlot (shape));
    if (axis.contains (event.getPosition()) && event.x > axis.getRight() - 40)
    {
        followLatest = true;
        selectBoundaryEvent (true);
        refreshBandEnvelope();
        repaint();
        return;
    }
    if (! selectsAt (shape, event.getPosition()) || ! attack_ui::validTimeline (latest, rate))
        return;
    followLatest = false;
    selectNearestEventAtX (event.x);
    refreshBandEnvelope();
    repaint();
}

void AttackComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! selectsAt (layout(), event.getPosition()) || ! attack_ui::validTimeline (latest, rate))
        return;
    followLatest = false;
    selectNearestEventAtX (event.x);
    refreshBandEnvelope();
    repaint();
}

// Hover help for the band (INV-S40): the chips, the HEAD / TAIL panes and the band lanes. The
// whole-signal view keeps its silence.
juce::String AttackComponent::tooltipAt (const attack_ui::Layout& shape, juce::Point<int> point) const
{
    for (std::size_t choice = 0; choice < attack_band::choiceCount; ++choice)
        if (rectangleOf (attack_band::chipCell (shape, presentationContext, choice)).contains (point))
            return choice != 0 && choice == chosenBand && preBand() == attack_band::PreBand::predates
                ? attack_band_painter::predatesTooltip()
                : attack_band_painter::chipTooltip (static_cast<std::uint8_t> (choice));
    if (chosenBand == 0)
        return {};
    if (bandPanes (shape))
    {
        if (rectangleOf (attack_band::headPane (shape)).contains (point))
            return attack_band_painter::paneTooltip (true);
        if (rectangleOf (attack_band::tailPane (shape)).contains (point))
            return attack_band_painter::paneTooltip (false);
    }
    for (std::size_t index = 0; index < attack_ui::laneCount; ++index)
    {
        const auto cell = shape.arrangement == attack_ui::Arrangement::lanes
            ? shape.lanes[index] : attack_ui::lineCell (shape, index);
        if (rectangleOf (cell).contains (point))
            return attack_band_painter::laneTooltip (attack_lanes::bandLanes[index]);
    }
    return {};
}

void AttackComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto tip = tooltipAt (layout(), event.getPosition());
    if (tip != getTooltip())
        setTooltip (tip);
}

void AttackComponent::mouseExit (const juce::MouseEvent&)
{
    if (getTooltip().isNotEmpty())
        setTooltip ({});
}

void AttackComponent::selectNearestEventAtX (int x) noexcept
{
    const auto plot = rectangleOf (attack_ui::historyPlot (layout()));
    const auto first = latest - attack_ui::windowSamples (rate);
    const auto requested = first + static_cast<std::int64_t> (
        static_cast<long double> (x - plot.getX()) * attack_ui::windowSamples (rate)
        / juce::jmax (1, plot.getWidth() - 1));
    std::int64_t bestDistance = std::numeric_limits<std::int64_t>::max();
    for (std::uint32_t item = 0; item < laneModel.count; ++item)
    {
        const auto& hit = laneModel.hits[item];
        if (! hit.selectable || ! attack_ui::eventIsVisible (hit.sample, latest, rate))
            continue;
        const auto distance = hit.sample > requested ? hit.sample - requested
                                                     : requested - hit.sample;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            selectedEventSample = hit.sample;
        }
    }
}

// LIVE and END follow the newest hit worth showing, HOME the oldest: with a band, one with its band
// stated before one still measuring, and that before one never measured (followRank).
void AttackComponent::selectBoundaryEvent (bool selectLast) noexcept
{
    int bestRank = -1;
    auto selected = std::int64_t { -1 };
    for (std::uint32_t item = 0; item < laneModel.count; ++item)
    {
        const auto rank = followRank (item);
        const auto sample = laneModel.hits[item].sample;
        if (rank < 0 || rank < bestRank)
            continue;
        if (rank > bestRank || (selectLast ? sample > selected : sample < selected))
        {
            bestRank = rank;
            selected = sample;
        }
    }
    if (bestRank >= 0)
        selectedEventSample = selected;
}

void AttackComponent::selectAdjacentEvent (bool moveRight) noexcept
{
    followLatest = false;
    auto selected = moveRight ? std::numeric_limits<std::int64_t>::max()
                              : std::numeric_limits<std::int64_t>::min();
    for (std::uint32_t item = 0; item < laneModel.count; ++item)
    {
        const auto& hit = laneModel.hits[item];
        if (! hit.selectable || ! attack_ui::eventIsVisible (hit.sample, latest, rate))
            continue;
        if ((moveRight && hit.sample > selectedEventSample && hit.sample < selected)
            || (! moveRight && hit.sample < selectedEventSample && hit.sample > selected))
            selected = hit.sample;
    }
    if (selected == std::numeric_limits<std::int64_t>::min()
        || selected == std::numeric_limits<std::int64_t>::max())
        selectBoundaryEvent (! moveRight);
    else
        selectedEventSample = selected;
}

bool AttackComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        selectAdjacentEvent (key == juce::KeyPress::rightKey);
    else if (key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey)
    {
        followLatest = key == juce::KeyPress::endKey;
        selectBoundaryEvent (key == juce::KeyPress::endKey);
    }
    else
        return false;
    refreshBandEnvelope();
    repaint();
    return true;
}
}
