#pragma once

#include "kirin_hypha_abi_contract.h"
#include "kirin_hypha_channels.h"
#include "kirin_hypha_ffi.h"

#include <cstddef>

/**
    Checks that the Rust library this shell is linked against was built from the same ABI as the
    headers it was compiled with.

    static_assert covers the headers and this translation unit. It cannot see the staticlib, which
    was built earlier and separately: pair a stale one with current headers and everything compiles,
    links, and reads every field at the wrong offset. The result is numbers that look like
    measurements and are not, which is the failure Kirin treats as worse than missing a feature
    (D-13). So the shell asks the library what it was built with, once, and refuses to create an
    engine if the answer differs.
*/
namespace kirin
{

inline KirinAbiContract expectedAbiContract() noexcept
{
    return {
        KIRIN_ABI_REVISION,
        KIRIN_OBSERVATORY_FRAME_VERSION,
        KIRIN_MAX_CHANNELS,
        KIRIN_MONO_SUM_BAND_COUNT,
        KIRIN_STEREO_FIELD_BINS,
        0u,
        sizeof (KirinMeterSession),
        alignof (KirinMeterSession),
        sizeof (KirinObservatoryFrame),
        sizeof (KirinMeasureResult),
        sizeof (KirinDelta),
        sizeof (KirinMeterHistoryEntry),
        offsetof (KirinMeterHistoryEntry, measurement_epoch),
        offsetof (KirinMeterSession, channels),
        offsetof (KirinMeterSession, sample_peak_dbfs),
        offsetof (KirinMeterSession, channel_positions),
        offsetof (KirinMeterSession, measurement_epoch),
    };
}

/** True only for an exact contract; older/newer revisions and short frames fail closed. */
inline bool abiMatches (const KirinAbiContract& lib) noexcept
{
    const auto expected = expectedAbiContract();
    return lib.revision == expected.revision
        && lib.observatory_frame_version == expected.observatory_frame_version
        && lib.max_channels == expected.max_channels
        && lib.mono_sum_band_count == expected.mono_sum_band_count
        && lib.stereo_field_bins == expected.stereo_field_bins
        && lib.reserved == expected.reserved
        && lib.meter_session_size == expected.meter_session_size
        && lib.meter_session_align == expected.meter_session_align
        && lib.observatory_frame_size == expected.observatory_frame_size
        && lib.measure_result_size == expected.measure_result_size
        && lib.delta_size == expected.delta_size
        && lib.meter_history_entry_size == expected.meter_history_entry_size
        && lib.meter_history_entry_epoch_offset
               == expected.meter_history_entry_epoch_offset
        && lib.meter_session_channels_offset == expected.meter_session_channels_offset
        && lib.meter_session_sample_peak_offset == expected.meter_session_sample_peak_offset
        && lib.meter_session_channel_positions_offset
               == expected.meter_session_channel_positions_offset
        && lib.meter_session_measurement_epoch_offset
               == expected.meter_session_measurement_epoch_offset;
}

/** True when the linked library reports exactly the ABI these headers describe. */
inline bool abiMatchesLinkedLibrary() noexcept
{
    KirinAbiContract lib {};
    kirin_hypha_abi_contract (&lib);
    return abiMatches (lib);
}

/** The same answer, computed once per process. The contract cannot change while it runs. */
inline bool abiMatchesLinkedLibraryOnce() noexcept
{
    static const bool matches = abiMatchesLinkedLibrary();
    return matches;
}

} // namespace kirin
