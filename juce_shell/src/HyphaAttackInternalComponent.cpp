#include "HyphaAttackInternalComponent.h"

#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha
{
void AttackInternalComponent::setSnapshot (const KirinAttackEventBatch& events,
                                           std::int64_t latestSample,
                                           std::uint32_t sampleRate,
                                           std::uint64_t generation,
                                           const KirinAttackStats& stats)
{
    eventBatch = events;
    runtimeStats = stats;
    latest = latestSample;
    rate = sampleRate;
    currentGeneration = generation;
    repaint();
}

void AttackInternalComponent::clearSnapshot()
{
    eventBatch = {};
    runtimeStats = {};
    latest = -1;
    rate = 0;
    currentGeneration = 0;
    repaint();
}

void AttackInternalComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto header = bounds.removeFromTop (attack_ui::headerHeight);
    auto axis = bounds.removeFromBottom (attack_ui::axisLabelHeight);
    const auto plot = bounds.reduced (1, 1);

    g.setFont (monoFont (10.0f));
    g.setColour (COL_SPECTRUM_DELTA);
    g.drawText ("ATTACK / DRUM / INTERNAL", header, juce::Justification::centredLeft);

    const bool running = runtimeStats.available != 0
                      && runtimeStats.enabled != 0
                      && runtimeStats.worker_running != 0;
    if (! running || ! attack_ui::validTimeline (latest, rate))
    {
        g.setColour (COL_MUTED);
        g.drawText (runtimeStats.available == 0 ? "UNAVAILABLE" : "WARMING UP",
                    plot, juce::Justification::centred);
        return;
    }

    const auto baseline = static_cast<float> (plot.getBottom() - 1);
    g.setColour (dim (COL_MUTED, 0.8f));
    g.drawHorizontalLine (juce::roundToInt (baseline),
                          static_cast<float> (plot.getX()),
                          static_cast<float> (plot.getRight()));

    std::uint32_t visibleCount = 0;
    const auto count = juce::jmin (eventBatch.count,
                                   static_cast<std::uint32_t> (KIRIN_ATTACK_EVENT_BATCH_CAPACITY));
    g.setColour (COL_SPECTRUM_DELTA_BR);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (eventBatch.events[index].generation != currentGeneration
            || eventBatch.events[index].sample_rate != rate)
            continue;
        const auto x = attack_ui::eventX (eventBatch.events[index].event_sample,
                                          latest, rate, plot.getWidth());
        if (x < 0)
            continue;
        ++visibleCount;
        const auto screenX = static_cast<float> (plot.getX() + x);
        g.drawVerticalLine (juce::roundToInt (screenX),
                            baseline - attack_ui::eventStemHeight, baseline);
    }

    g.setFont (monoFont (9.0f));
    g.setColour (COL_MUTED);
    g.drawText ("-6 s", axis.removeFromLeft (40), juce::Justification::centredLeft);
    g.drawText ("NOW", axis.removeFromRight (36), juce::Justification::centredRight);
    g.setColour (COL_NORMAL);
    g.drawText (juce::String (visibleCount) + " EVENTS", axis, juce::Justification::centred);
}
}
