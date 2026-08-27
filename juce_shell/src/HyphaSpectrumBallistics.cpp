#include "HyphaSpectrumBallistics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
    constexpr float kSnapEpsilonDb = 0.001f;

    bool sameFloatBits (float left, float right) noexcept
    {
        return std::memcmp (&left, &right, sizeof (float)) == 0;
    }

    float responseCoefficient (float elapsedSeconds, float timeConstantSeconds,
                               float responseScale) noexcept
    {
        const float scaledTime = timeConstantSeconds * responseScale;
        return 1.0f - std::exp (-elapsedSeconds / scaledTime);
    }

    float advanceTowards (float value, float target, float coefficient) noexcept
    {
        const float next = value + (target - value) * coefficient;
        return std::abs (target - next) <= kSnapEpsilonDb ? target : next;
    }
}

bool SpectrumBallistics::sameDomain (const KirinSpectrumView& left,
                                     const KirinSpectrumView& right) noexcept
{
    return left.sample_rate == right.sample_rate
        && sameFloatBits (left.min_hz, right.min_hz)
        && sameFloatBits (left.max_hz, right.max_hz);
}

float SpectrumBallistics::responseScaleForFrequency (float hz) noexcept
{
    if (hz <= spectrum_motion::lowResponseEndHz)
        return spectrum_motion::lowResponseTimeScale;
    if (hz >= spectrum_motion::fullResponseStartHz)
        return 1.0f;
    const float position = std::log (hz / spectrum_motion::lowResponseEndHz)
                         / std::log (spectrum_motion::fullResponseStartHz
                                     / spectrum_motion::lowResponseEndHz);
    const float blend = position * position * (3.0f - 2.0f * position);
    return spectrum_motion::lowResponseTimeScale
         + (1.0f - spectrum_motion::lowResponseTimeScale) * blend;
}

float SpectrumBallistics::advanceMagnitude (float value, float target,
                                            float elapsedSeconds,
                                            float responseScale) noexcept
{
    const float timeConstant = target >= value
                             ? spectrum_motion::magnitudeAttackSeconds
                             : spectrum_motion::magnitudeReleaseSeconds;
    return advanceTowards (value, target,
                           responseCoefficient (elapsedSeconds, timeConstant, responseScale));
}

float SpectrumBallistics::advanceDelta (float value, float target, float elapsedSeconds,
                                        float responseScale) noexcept
{
    const bool sameSign = (value > 0.0f && target > 0.0f)
                       || (value < 0.0f && target < 0.0f);
    const bool movingAwayFromZero = value == 0.0f
                                 || (sameSign && std::abs (target) >= std::abs (value));
    const float timeConstant = movingAwayFromZero
                             ? spectrum_motion::deltaAwaySeconds
                             : spectrum_motion::deltaReturnSeconds;
    return advanceTowards (value, target,
                           responseCoefficient (elapsedSeconds, timeConstant, responseScale));
}

bool SpectrumBallistics::setTarget (const KirinSpectrumView& next) noexcept
{
    if (! haveFrame || ! sameDomain (target, next))
    {
        current = next;
        target = next;
        haveFrame = true;
        return true;
    }
    target = next;
    current.status = next.status;
    current.has_data = next.has_data;
    current.sample_rate = next.sample_rate;
    current.min_hz = next.min_hz;
    current.max_hz = next.max_hz;
    return false;
}

bool SpectrumBallistics::advance (float elapsedSeconds) noexcept
{
    if (! haveFrame || ! std::isfinite (elapsedSeconds) || elapsedSeconds <= 0.0f)
        return false;
    const float step = std::min (elapsedSeconds, spectrum_motion::maximumStepSeconds);
    const float frequencyRatio = target.max_hz / target.min_hz;
    bool changed = false;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const float bandUnit = (static_cast<float> (index) + 0.5f)
                             / static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT);
        const float frequency = target.min_hz * std::pow (frequencyRatio, bandUnit);
        const float responseScale = responseScaleForFrequency (frequency);
        const float nextPre = advanceMagnitude (current.pre_dbfs[index],
                                                target.pre_dbfs[index], step, responseScale);
        const float nextPost = advanceMagnitude (current.post_dbfs[index],
                                                 target.post_dbfs[index], step, responseScale);
        const float nextDelta = advanceDelta (current.display_db[index],
                                              target.display_db[index], step, responseScale);
        changed = changed || ! sameFloatBits (nextPre, current.pre_dbfs[index])
                          || ! sameFloatBits (nextPost, current.post_dbfs[index])
                          || ! sameFloatBits (nextDelta, current.display_db[index]);
        current.pre_dbfs[index] = nextPre;
        current.post_dbfs[index] = nextPost;
        current.display_db[index] = nextDelta;
    }
    return changed;
}

void SpectrumBallistics::reset() noexcept
{
    current = {};
    target = {};
    haveFrame = false;
}
}
