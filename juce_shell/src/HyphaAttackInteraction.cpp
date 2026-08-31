#include "HyphaAttackInternalComponent.h"

#include <limits>

#include "HyphaAttackUiContract.h"

namespace hypha
{
AttackInternalComponent::AttackInternalComponent()
{
    setWantsKeyboardFocus (true);
}

void AttackInternalComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (event.y < attack_ui::headerHeight && event.x > getWidth() - 130)
    {
        overlayMode = ! overlayMode;
        repaint();
        return;
    }
    const auto timeline = timelineBounds();
    if (! timeline.contains (event.getPosition()) || ! attack_ui::validTimeline (latest, rate))
        return;
    const auto first = latest - attack_ui::windowSamples (rate);
    const auto requested = first + static_cast<std::int64_t> (
        static_cast<long double> (event.x - timeline.getX()) * attack_ui::windowSamples (rate)
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
    repaint();
}

void AttackInternalComponent::selectBoundaryEvent (bool selectLast) noexcept
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

void AttackInternalComponent::selectAdjacentEvent (bool moveRight) noexcept
{
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

bool AttackInternalComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        selectAdjacentEvent (key == juce::KeyPress::rightKey);
    else if (key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey)
        selectBoundaryEvent (key == juce::KeyPress::endKey);
    else
        return false;
    repaint();
    return true;
}
}
