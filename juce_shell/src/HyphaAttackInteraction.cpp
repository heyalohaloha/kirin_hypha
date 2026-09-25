#include "HyphaAttackComponent.h"

#include <limits>

#include "HyphaAttackUiContract.h"

namespace hypha
{
AttackComponent::AttackComponent()
{
    setTitle ("DRUM Attack");
    setDescription ("Drum transient facts for TRACK/STEM; not a 2MIX onset detector.");
    setWantsKeyboardFocus (true);
}

// HISTORY, the axis and every lane share one plot column; any point in it selects by time.
bool AttackComponent::selectsAt (const attack_ui::Layout& shape, juce::Point<int> point) const noexcept
{
    const auto history = rectangleOf (attack_ui::historyPlot (shape));
    if (history.isEmpty())
        return false;
    const auto bottom = shape.arrangement == attack_ui::Arrangement::lanes
        ? shape.lanes.back().bottom() : shape.axis.bottom();
    return point.x >= history.getX() && point.x < history.getRight()
        && point.y >= shape.history.y && point.y < bottom;
}

void AttackComponent::mouseDown (const juce::MouseEvent& event)
{
    if (isShowing()) grabKeyboardFocus();
    if (event.y < attack_ui::titleRowHeight (presentationContext)
        && event.x > getWidth() - viewControlWidth())
    {
        overlayMode = ! overlayMode;
        repaint();
        return;
    }
    const auto shape = layout();
    const auto axis = rectangleOf (attack_ui::axisPlot (shape));
    if (axis.contains (event.getPosition()) && event.x > axis.getRight() - 40)
    {
        followLatest = true;
        selectBoundaryEvent (true);
        repaint();
        return;
    }
    if (! selectsAt (shape, event.getPosition()) || ! attack_ui::validTimeline (latest, rate))
        return;
    followLatest = false;
    selectNearestEventAtX (event.x);
    repaint();
}

void AttackComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! selectsAt (layout(), event.getPosition()) || ! attack_ui::validTimeline (latest, rate))
        return;
    followLatest = false;
    selectNearestEventAtX (event.x);
    repaint();
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

void AttackComponent::selectBoundaryEvent (bool selectLast) noexcept
{
    auto selected = selectLast ? std::numeric_limits<std::int64_t>::min()
                               : std::numeric_limits<std::int64_t>::max();
    for (std::uint32_t item = 0; item < laneModel.count; ++item)
    {
        const auto& hit = laneModel.hits[item];
        if (! hit.selectable || ! attack_ui::eventIsVisible (hit.sample, latest, rate))
            continue;
        if ((selectLast && hit.sample > selected) || (! selectLast && hit.sample < selected))
            selected = hit.sample;
    }
    if (selected != std::numeric_limits<std::int64_t>::min()
        && selected != std::numeric_limits<std::int64_t>::max())
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
    repaint();
    return true;
}
}
