#include "ReferenceTonalCapture.h"

#include <cmath>

namespace hypha::reference_audition
{
namespace
{
bool sha256 (const juce::String& value) noexcept
{
    if (value.length() != 64) return false;
    for (const auto character : value)
        if (! ((character >= '0' && character <= '9')
               || (character >= 'a' && character <= 'f'))) return false;
    return true;
}
}

bool CaptureTonalSummary::valid() const noexcept
{
    if (sampleRate < 40'000 || sampleRate > 768'000 || channels < 1 || channels > 2
        || frames < 1 || validBits == 0) return false;
    for (size_t band = 0; band < 60; ++band)
        if ((validBits & (std::uint64_t (1) << band)) != 0
            && (! std::isfinite (p10[band]) || ! std::isfinite (median[band])
                || ! std::isfinite (p90[band]) || p10[band] > median[band]
                || median[band] > p90[band] || p10[band] < -120.0f || p90[band] > 0.001f)) return false;
    const bool noReceipt = artifactSha256.isEmpty() && recoveryKey.isEmpty() && artifactBytes == 0;
    const bool receipt = sha256 (artifactSha256) && sha256 (recoveryKey)
        && artifactBytes > 0 && artifactBytes <= 16 * 1024 * 1024;
    return noReceipt || receipt;
}
}
