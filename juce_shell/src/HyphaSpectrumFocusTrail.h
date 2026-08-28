#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "HyphaSpectrumUiContract.h"
#include "kirin_hypha_ffi.h"

namespace hypha::spectrum_focus
{
    constexpr size_t focusTrailSeconds = 6u;
    constexpr size_t focusTrailCapacity = static_cast<size_t> (
        ui_contract::spectrumPresentationHz) * focusTrailSeconds;

    using DeltaBins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;

    enum class AppendResult
    {
        appended,
        duplicateIgnored,
        discontinuityReset,
        rejected,
    };

    // UI-thread-only, fixed-capacity presentation history. It stores exact display snapshots and
    // their shared PRE/POST sample endpoints so paint never invents a time point or smooths live Δ.
    class FocusTrailHistory final
    {
    public:
        AppendResult append (int64_t presentationEndSamples,
                             uint32_t sampleRate,
                             const DeltaBins& displayDelta) noexcept;
        void clear() noexcept;

        bool empty() const noexcept { return count == 0u; }
        size_t size() const noexcept { return count; }
        uint32_t currentSampleRate() const noexcept { return sampleRate; }
        int64_t endpointAt (size_t chronologicalIndex) const noexcept;
        float valueAt (size_t chronologicalIndex, float normalisedBand) const noexcept;
        double ageSecondsAt (size_t chronologicalIndex) const noexcept;

    private:
        struct Frame
        {
            int64_t presentationEndSamples = 0;
            DeltaBins displayDelta {};
        };

        size_t physicalIndex (size_t chronologicalIndex) const noexcept;

        std::array<Frame, focusTrailCapacity> frames {};
        size_t start = 0u;
        size_t count = 0u;
        uint32_t sampleRate = 0u;
    };

    static_assert (focusTrailCapacity == 180u,
                   "Focus Trail must retain exactly six seconds at the 30 Hz presentation rate");
    static_assert (sizeof (FocusTrailHistory) < 256u * 1024u,
                   "Focus Trail UI storage must remain below the 256 KiB budget");
}
