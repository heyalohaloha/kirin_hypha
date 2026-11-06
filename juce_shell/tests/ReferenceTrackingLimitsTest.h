#pragma once
#include "../src/reference_audition/ReferenceTrackingGain.h"

namespace reference_tracking_limits
{
inline bool closeTo (double value, double expected, double tolerance = 1.0e-9)
{ return std::abs (value - expected) <= tolerance; }
inline void verify()
{
    namespace r = ref;
    require (closeTo (r::referenceGainHeadroomDb (-6.0, -2.0), 5.0) && closeTo (r::referenceGainHeadroomDb (-6.0, -0.5), 5.5)
                 && closeTo (r::referenceGainHeadroomDb (-0.5, -3.0), 0.0) && closeTo (r::referenceGainHeadroomDb (NAN, -3.0), 0.0),
             "the ceiling is the louder of -1 dBTP, A's peak and the reference's own peak");
    require (r::trackingStep (2.3, 2.0, -6.0, -2.0).action == r::TrackingAction::keep
                 && r::trackingStep (NAN, 2.0, -6.0, -2.0).action == r::TrackingAction::keep
                 && r::trackingStep (150.0, 2.0, -6.0, -2.0).action == r::TrackingAction::keep,
             "within 0.5 dB, or nothing measured, keeps the gain");
    const auto down = r::trackingStep (-10.0, 2.0, -6.0, -2.0);
    const auto up = r::trackingStep (4.5, 2.0, -6.0, -2.0);
    const auto withinShortfall = r::trackingStep (5.5, 2.0, -6.0, -2.0);  // 上限（+5.0）に 0.5 dB 届かない
    const auto over = r::trackingStep (5.6, 2.0, -6.0, -2.0);  // 0.6 dB 届かない
    require (down.action == r::TrackingAction::move && closeTo (down.gainDb, -10.0)
                 && up.action == r::TrackingAction::move && closeTo (up.gainDb, 4.5) && closeTo (up.shortfallDb, 0.0)
                 && withinShortfall.action == r::TrackingAction::move && closeTo (withinShortfall.gainDb, 5.0, 1.0e-9) && closeTo (withinShortfall.shortfallDb, 0.5, 1.0e-9)
                 && over.action == r::TrackingAction::move && closeTo (over.gainDb, 5.0, 1.0e-9) && closeTo (over.shortfallDb, 0.6, 1.0e-9),
             "lowering always moves; raising moves within the ceiling and otherwise stays at the ceiling, following");
    require (closeTo (r::referencePeakShortfallDb (5.5, -6.0, -2.0), 0.5, 1.0e-9) && r::referencePeakShortfallDb (4.0, -6.0, -2.0) == 0.0
                 && closeTo (r::referencePeakShortfallDb (10.0, -2.0, -20.0, -8.0), 1.0, 1.0e-9),
             "the shortfall is how far the gain would pass the ceiling, after any A lowering");
    for (const double held : { 0.0, -8.0 })
    {
        const auto crossed = r::trackingStep (5.2 - held, 4.6 - held, -6.0, -2.0, 4.6 - held, held);
        const auto settled = r::trackingStep (5.2 - held, crossed.gainDb, -6.0, -2.0, 4.6 - held, held);
        const auto tolerated = r::trackingStep (5.1 - held, 4.8 - held, -6.0, -2.0, 4.8 - held, held);
        const auto coincident = r::trackingStep (5.2 - held, 4.6 - held, -6.0, -2.0, -1.0 - held, held);
        const auto returned = r::trackingStep (4.0 - held, crossed.gainDb, -6.0, -2.0, 4.6 - held, held);
        require (crossed.action == r::TrackingAction::move && closeTo (crossed.gainDb, 5.0 - held)
                     && closeTo (crossed.shortfallDb, 0.2) && crossed.rangeShortfallDb == 0.0
                     && settled.action == r::TrackingAction::keep && closeTo (settled.shortfallDb, 0.2)
                     && tolerated.action == r::TrackingAction::keep && closeTo (tolerated.gainDb, 4.8 - held)
                     && closeTo (tolerated.shortfallDb, 0.3)
                     && coincident.action == r::TrackingAction::move && closeTo (coincident.gainDb, 5.0 - held)
                     && closeTo (coincident.shortfallDb, 0.2) && closeTo (coincident.rangeShortfallDb, 0.2)
                     && returned.action == r::TrackingAction::move && closeTo (returned.gainDb, 4.0 - held)
                     && returned.shortfallDb == 0.0,
                 "crossing the ceiling closes a difference over tolerance, reports the applied residual and follows back");
    }
    // Both sides of the MATCH range: inside, exactly at the edge, beyond it, held, then back inside.
    for (const double direction : { -1.0, 1.0 })
    {
        for (const double distance : { 5.9, 6.0, 6.1, 8.0 })
        {
            const auto wanted = 2.0 + direction * distance;
            const auto step = r::trackingStep (wanted, 2.0, -30.0, -2.0, 2.0);
            const auto target = 2.0 + direction * std::min (6.0, distance);
            require (step.action == r::TrackingAction::move && closeTo (step.gainDb, target)
                         && closeTo (step.rangeShortfallDb, wanted - target) && step.shortfallDb == 0.0,
                     "a range boundary clamps the gain and reports the signed residual without stopping");
        }
        const auto held = r::trackingStep (2.0 + direction * 8.0, 2.0 + direction * 6.0, -30.0, -2.0, 2.0);
        const auto back = r::trackingStep (2.0 + direction * 5.0, held.gainDb, -30.0, -2.0, 2.0);
        require (held.action == r::TrackingAction::keep && closeTo (held.rangeShortfallDb, direction * 2.0)
                     && back.action == r::TrackingAction::move && closeTo (back.gainDb, 2.0 + direction * 5.0)
                     && back.rangeShortfallDb == 0.0,
                 "holding at either edge continues to measure and follows again inside the original range");
    }
    const auto peakWins = r::trackingStep (12.0, 8.0, -2.0, -8.0, 8.0);
    const auto slightPeak = r::trackingStep (1.2, 1.2, -2.0, -8.0, 1.2);
    const auto heldA = r::trackingStep (20.0, 8.0, -2.0, -8.0, 8.0, -8.0);
    require (peakWins.action == r::TrackingAction::move && closeTo (peakWins.gainDb, 1.0)
                 && closeTo (peakWins.shortfallDb, 11.0) && peakWins.rangeShortfallDb == 0.0
                 && slightPeak.action == r::TrackingAction::move && closeTo (slightPeak.gainDb, 1.0)
                 && closeTo (heldA.gainDb, 9.0) && closeTo (heldA.shortfallDb, 11.0),
             "the peak ceiling wins over the lower range edge and tolerance, respecting approved A attenuation");
    require (r::trackingStep (NAN, -4.0, -30.0, -2.0, 2.0).action == r::TrackingAction::keep
                 && r::trackingStep (-4.2, -4.0, -30.0, -2.0, 2.0).action == r::TrackingAction::keep,
             "missing evidence and changes within tolerance preserve the gain at the edge");

    r::TrackingGainRamp ramp;
    ramp.settle (1.0f);
    float first = ramp.next (2.0f, 2400), middle = 0.0f, last = 0.0f;
    for (int frame = 2; frame <= 2400; ++frame) (frame == 1200 ? middle : last) = ramp.next (2.0f, 2400);
    require (closeTo (first, 1.0 + 1.0 / 2400.0, 1.0e-6) && closeTo (middle, 1.5, 1.0e-6) && closeTo (last, 2.0, 0.0)
                 && closeTo (ramp.next (2.0f, 2400), 2.0, 0.0),
             "a new gain arrives on a straight 50 ms line and then stays");
    ramp.next (1.0f, 2400);
    ramp.settle (0.5f);
    require (closeTo (ramp.next (0.5f, 2400), 0.5, 0.0), "while nothing is heard the gain is set at once");
}

}
