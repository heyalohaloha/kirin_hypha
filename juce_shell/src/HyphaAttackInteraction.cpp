#include "HyphaAttackComponent.h"

#include <limits>

#include "HyphaAttackUiContract.h"

namespace hypha
{
AttackComponent::AttackComponent()
{
    setWantsKeyboardFocus (true);
}

void AttackComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (event.y < attack_ui::headerHeight
        && event.x > getWidth() - attack_ui::modeControlWidth (getWidth()))
    {
        overlayMode = ! overlayMode;
        repaint();
        return;
    }
    const auto scrub = scrubBounds();
    if (scrub.contains (event.getPosition()) && event.x > scrub.getRight() - 40)
    {
        followLatest = true;
        selectBoundaryEvent (true);
        repaint();
        return;
    }
    const auto timeline = timelineBounds();
    if ((! timeline.contains (event.getPosition()) && ! scrub.contains (event.getPosition()))
        || ! attack_ui::validTimeline (latest, rate))
        return;
    followLatest = false;
    selectNearestEventAtX (event.x);
    repaint();
}

void AttackComponent::mouseDrag (const juce::MouseEvent& event)
{
    if ((! timelineBounds().contains (event.getPosition())
         && ! scrubBounds().contains (event.getPosition()))
        || ! attack_ui::validTimeline (latest, rate))
        return;
    followLatest = false;
    selectNearestEventAtX (event.x);
    repaint();
}

void AttackComponent::selectNearestEventAtX (int x) noexcept
{
    const auto timeline = timelineBounds();
    const auto first = latest - attack_ui::windowSamples (rate);
    const auto requested = first + static_cast<std::int64_t> (
        static_cast<long double> (x - timeline.getX()) * attack_ui::windowSamples (rate)
        / juce::jmax (1, timeline.getWidth() - 1));
    std::int64_t bestDistance = std::numeric_limits<std::int64_t>::max();
    const auto consider = [&] (std::int64_t sample)
    {
        if (! attack_ui::eventIsVisible (sample, latest, rate))
            return;
        const auto distance = sample > requested ? sample - requested : requested - sample;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            selectedEventSample = sample;
        }
    };
    if (pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE)
    {
        const auto count = juce::jmin (
            pairEventBatch.count,
            static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            consider (pairEventBatch.events[index].event_sample);
    }
    else
    {
        const auto count = juce::jmin (
            detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            consider (detailBatch.details[index].event_sample);
    }
}

void AttackComponent::selectBoundaryEvent (bool selectLast) noexcept
{
    auto selected = selectLast ? std::numeric_limits<std::int64_t>::min()
                               : std::numeric_limits<std::int64_t>::max();
    const auto consider = [&] (std::int64_t sample)
    {
        if (! attack_ui::eventIsVisible (sample, latest, rate))
            return;
        if ((selectLast && sample > selected) || (! selectLast && sample < selected))
            selected = sample;
    };
    if (pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE)
    {
        const auto count = juce::jmin (
            pairEventBatch.count,
            static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            consider (pairEventBatch.events[index].event_sample);
    }
    else
    {
        const auto count = juce::jmin (
            detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            consider (detailBatch.details[index].event_sample);
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
    const auto consider = [&] (std::int64_t sample)
    {
        if (! attack_ui::eventIsVisible (sample, latest, rate))
            return;
        if ((moveRight && sample > selectedEventSample && sample < selected)
            || (! moveRight && sample < selectedEventSample && sample > selected))
            selected = sample;
    };
    if (pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE)
    {
        const auto count = juce::jmin (
            pairEventBatch.count,
            static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            consider (pairEventBatch.events[index].event_sample);
    }
    else
    {
        const auto count = juce::jmin (
            detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            consider (detailBatch.details[index].event_sample);
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
