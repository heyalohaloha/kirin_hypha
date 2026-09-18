#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"
#include "HyphaMonoSumHistory.h"

// MONO — how much of each third-octave band survives the mono sum, from KirinMeterSession.
//
// The vertical axis is split. Identical channels read 0 dB, an ordinary mix about -0.7 dB, a very
// wide one -2.6 dB and a hard-panned source -3.01 dB, so everything a user acts on sits between
// 0 and -3 dB. A straight 0..-24 dB scale squeezes that into the top twelfth of the plot and
// spends half the height on a region where -15 and -20 lead to the same conclusion. The top half
// carries 0..-6 dB and the bottom half -6..-24 dB.
namespace hypha::mono_sum_curve
{
/// Height of one band value on the split scale. Values outside the scale stop at its edge.
float yForDb (float db, juce::Rectangle<float> plot) noexcept;

/// True when the band's low edge holds fewer than three cycles in one observation.
bool bandIsApproximate (size_t band, float approximateBelowHz) noexcept;

/// `showTitle` is false where the panel's own title row already names MONO, which is how the
/// smallest size fits it without spending two rows on labels.
void paint (juce::Graphics&,
            juce::Rectangle<int> area,
            const KirinMeterSession&,
            const mono_sum_history::History&,
            bool available,
            bool compact,
            bool showTitle,
            presentation::Context);

/// Ink for one band of the six-second field: none at 0 dB, most at the display floor. The split
/// scale decides it, so a value's weight in the field matches its height on the curve above.
uint8_t fieldAlphaStepFor (float db) noexcept;

/// What the readout says beside the title: the unit when bands are measured, and why not when
/// they are not.
juce::String stateText (const KirinMeterSession&, bool available);
bool hasBands (const KirinMeterSession&, bool available) noexcept;
}
