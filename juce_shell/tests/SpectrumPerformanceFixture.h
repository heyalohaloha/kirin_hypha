#pragma once
#include "kirin_hypha_ffi.h"
#include <cmath>

namespace hypha::tests
{
// Same densely modulated stereo fixture as the complete render contract.
inline KirinSpectrumView spectrumPerformanceFixture()
{
    KirinSpectrumView s {};
    s.status = KIRIN_SPECTRUM_ACTIVE;
    s.has_data = s.post_has_data = 1;
    s.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    s.channels = 2;
    s.sample_rate = 48'000;
    s.aperture_samples = 4'096;
    s.fft_size = 8'192;
    s.approximate_below_hz = 35.15625f;
    s.presentation_end_samples = 48'000;
    s.min_hz = 10.0f;
    s.max_hz = 22'000.0f;
    for (size_t i = 0; i < KIRIN_SPECTRUM_BAND_COUNT; ++i)
    {
        const float p = static_cast<float> (i) / (KIRIN_SPECTRUM_BAND_COUNT - 1u);
        const float body = -78.0f + 62.0f * std::exp (-std::pow ((p - 0.53f) / 0.42f, 2.0f));
        const float region = 0.38f + 0.62f * std::exp (-std::pow ((p - 0.61f) / 0.24f, 2.0f));
        s.display_db[i] = 14.0f * region * std::sin (static_cast<float> (i) * 0.065f);
        s.pre_dbfs[i] = body + 2.0f * std::sin (static_cast<float> (i) * 0.045f);
        s.post_dbfs[i] = s.pre_dbfs[i] + s.display_db[i];
    }
    return s;
}
}
