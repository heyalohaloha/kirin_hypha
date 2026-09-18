#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <juce_graphics/juce_graphics.h>

// A field of observations with time on the vertical axis: newest at the bottom, oldest at the
// top. FREQ draws the six-second POST Spectrum this way and SPACE draws MONO the same way, so the
// rule lives here once rather than once per screen.
//
// Each row is one slice of the span and shows the observation nearest its own instant. A row
// stays empty only when the nearest observation is further away than the cadence the source is
// actually publishing at, measured from the observations themselves. Placing each observation in
// the row its age falls in looked simpler and is wrong: a host publishes on its own buffer
// boundaries, so at 48 kHz a 1024-sample buffer delivers one observation per 42.7 ms and a
// 4096-sample buffer one per 85 ms. Either leaves rows empty that no measurement gap caused, and
// the field draws stripes. A real break stays visible because a break is long against the cadence
// around it, and a median is not moved by the few long intervals it creates.
namespace hypha::time_field
{
    // Both callers are far below this. The bound keeps the working arrays on the stack.
    inline constexpr size_t maximumObservations = 256u;

    struct Geometry
    {
        int rows = 0;
        int columns = 0;
        double spanSeconds = 0.0;
        // Two observations five seconds apart are not a cadence. Without the cap they would call
        // five seconds the cadence and fill the whole field between them.
        double slowestCredibleCadenceSeconds = 0.5;
    };

    /**
        Builds the field as one image, a column per source column and a row per slice of the span.

        `count` observations are enumerated oldest first, so `ageOf` falls as the index rises.
        `ageOf(index)` returns the observation's age in seconds; anything outside [0, span] is
        dropped. `valueAt(index, column)` returns that observation's value for the column.
        `alphaStepFor(value)` maps it to 0..255, where 0 draws nothing.

        Returns an invalid Image when there is nothing to draw, so the caller can skip the blit.
        The image is drawn with nearest-neighbour resampling: a smoothed stretch would blend
        neighbouring observations into pixels that were never measured, and would bridge a gap
        instead of showing it.
    */
    template <typename AgeOf, typename ValueAt, typename AlphaStepFor>
    juce::Image build (Geometry geometry,
                       size_t count,
                       AgeOf&& ageOf,
                       ValueAt&& valueAt,
                       juce::Colour ink,
                       AlphaStepFor&& alphaStepFor)
    {
        if (geometry.rows <= 0 || geometry.columns <= 0 || ! (geometry.spanSeconds > 0.0)
            || count == 0u)
            return {};

        const double rowSeconds = geometry.spanSeconds / (double) geometry.rows;

        std::array<double, maximumObservations> ages {};
        std::array<size_t, maximumObservations> source {};
        size_t inWindow = 0u;
        for (size_t index = 0u; index < count && inWindow < maximumObservations; ++index)
        {
            const double ageSeconds = ageOf (index);
            if (! (ageSeconds >= 0.0) || ageSeconds > geometry.spanSeconds)
                continue;
            ages[inWindow] = ageSeconds;
            source[inWindow] = index;
            ++inWindow;
        }
        if (inWindow == 0u)
            return {};

        // This source's own cadence: the median interval between the observations in the window.
        double cadenceSeconds = rowSeconds;
        if (inWindow > 1u)
        {
            std::array<double, maximumObservations> intervals {};
            const size_t intervalCount = inWindow - 1u;
            for (size_t index = 0u; index < intervalCount; ++index)
                intervals[index] = ages[index] - ages[index + 1u];
            const auto middle = intervals.begin() + (std::ptrdiff_t) (intervalCount / 2u);
            std::nth_element (intervals.begin(), middle,
                              intervals.begin() + (std::ptrdiff_t) intervalCount);
            cadenceSeconds = juce::jlimit (rowSeconds,
                                           geometry.slowestCredibleCadenceSeconds, *middle);
        }

        juce::Image image (juce::Image::ARGB, geometry.columns, geometry.rows, true,
                           juce::SoftwareImageType {});
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);

        // One premultiplied colour per 1/255 alpha step, so the inner loop is a table lookup.
        std::array<juce::PixelARGB, 256> tint {};
        for (size_t step = 0u; step < tint.size(); ++step)
            tint[step] = ink.withAlpha ((float) step / 255.0f).getPixelARGB();

        // Ages fall as the index rises and the rows ask for rising ages, so one walk covers both.
        size_t candidate = inWindow - 1u;
        bool painted = false;
        for (int fromBottom = 0; fromBottom < geometry.rows; ++fromBottom)
        {
            const double instant = ((double) fromBottom + 0.5) * rowSeconds;
            while (candidate > 0u
                   && std::abs (ages[candidate - 1u] - instant)
                          <= std::abs (ages[candidate] - instant))
                --candidate;
            if (std::abs (ages[candidate] - instant) > cadenceSeconds)
                continue;

            const auto observation = source[candidate];
            auto* line = (juce::PixelARGB*) pixels.getLinePointer (geometry.rows - 1 - fromBottom);
            for (int column = 0; column < geometry.columns; ++column)
            {
                const auto step = alphaStepFor (valueAt (observation, column));
                if (step == 0u)
                    continue;
                line[column] = tint[step];
                painted = true;
            }
        }
        return painted ? image : juce::Image {};
    }

    /** Blits a field built by `build` across the plot without inventing values between rows. */
    inline void draw (juce::Graphics& g, const juce::Image& field, juce::Rectangle<float> plot)
    {
        if (! field.isValid() || plot.isEmpty())
            return;

        const juce::Graphics::ScopedSaveState saved (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (field, plot, juce::RectanglePlacement::stretchToFit, false);
    }
}
