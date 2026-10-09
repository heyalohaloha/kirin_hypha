#pragma once
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_attack_summary_v2_ffi.h"
#include "kirin_hypha_attack_snapshot_ffi.h"
#include <juce_core/juce_core.h>

namespace hypha::attack_v2
{
juce::String factNumber (double);
// Raw endpoints and the display exponent stay separate; display rounding never changes evidence.
struct FormattedInterval
{
    juce::String value { "---" }, unit;
    int exponent = 0;
    bool valid = false;
    KirinSnapshotInterval raw {};
};
bool validInterval (const KirinSnapshotInterval&) noexcept;
FormattedInterval formatInterval (const KirinSnapshotInterval&, int decimals,
                                  const juce::String& unit, bool signedValue, bool exact);
juce::String reasonText (std::uint8_t reason, bool detailed = false);
juce::String words (const char* english, const char* japanese);
}
