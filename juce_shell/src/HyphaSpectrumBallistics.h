#pragma once

#include "kirin_hypha_ffi.h"

namespace hypha
{
namespace spectrum_motion
{
    constexpr float deltaAwaySeconds = 0.09f;
    constexpr float deltaReturnSeconds = 0.25f;
    constexpr float magnitudeAttackSeconds = 0.14f;
    constexpr float magnitudeReleaseSeconds = 0.40f;
    constexpr float lowResponseEndHz = 220.0f;
    constexpr float fullResponseStartHz = 1'200.0f;
    constexpr float lowResponseTimeScale = 1.25f;
    constexpr float maximumStepSeconds = 0.25f;
}

// Display-only motion for the POST Spectrum page. Measurement snapshots remain untouched; this
// state owns only the presented PRE, POST, and signed delta curves. A changed frequency domain is
// seeded immediately so values from another sample rate or pairing epoch cannot bleed across.
class SpectrumBallistics final
{
public:
    bool setTarget (const KirinSpectrumView& next) noexcept;
    bool advance (float elapsedSeconds) noexcept;
    void reset() noexcept;

    bool hasFrame() const noexcept { return haveFrame; }
    const KirinSpectrumView& frame() const noexcept { return current; }

private:
    static bool sameDomain (const KirinSpectrumView& left,
                            const KirinSpectrumView& right) noexcept;
    static float responseScaleForFrequency (float hz) noexcept;
    static float advanceMagnitude (float value, float target, float elapsedSeconds,
                                   float responseScale) noexcept;
    static float advanceDelta (float value, float target, float elapsedSeconds,
                               float responseScale) noexcept;

    KirinSpectrumView current {};
    KirinSpectrumView target {};
    bool haveFrame = false;
};
}
