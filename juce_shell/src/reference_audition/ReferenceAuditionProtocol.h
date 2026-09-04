#pragma once

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"

namespace hypha::reference_audition
{
    constexpr std::int64_t maximumPreparationBytes = 32 * 1024;
    constexpr std::int64_t maximumSourceReceiptBytes = 64 * 1024;
    constexpr std::int64_t maximumRuntimeReceiptBytes = 16 * 1024;
    constexpr std::int64_t maximumLeaseDurationMs = 10'000;

    bool safeId (const juce::String&) noexcept;
    bool safeSha256 (const juce::String&) noexcept;
    bool safeUuid (const juce::String&) noexcept;

    bool parsePreparation (const juce::var&, Preparation&) noexcept;
    bool parseSourceReceipt (const juce::var&, SourceReceipt&) noexcept;
}
