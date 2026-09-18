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

/** True when the linked library reports exactly the ABI these headers describe. */
inline bool abiMatchesLinkedLibrary() noexcept
{
    KirinAbiContract lib {};
    kirin_hypha_abi_contract (&lib);
    return lib.revision == KIRIN_ABI_REVISION
        && lib.observatory_frame_version == KIRIN_OBSERVATORY_FRAME_VERSION
        && lib.max_channels == KIRIN_MAX_CHANNELS
        && lib.mono_sum_band_count == KIRIN_MONO_SUM_BAND_COUNT
        && lib.stereo_field_bins == KIRIN_STEREO_FIELD_BINS
        && lib.meter_session_size == sizeof (KirinMeterSession)
        && lib.meter_session_align == alignof (KirinMeterSession)
        && lib.observatory_frame_size == sizeof (KirinObservatoryFrame)
        && lib.measure_result_size == sizeof (KirinMeasureResult)
        && lib.delta_size == sizeof (KirinDelta)
        && lib.meter_history_entry_size == sizeof (KirinMeterHistoryEntry)
        && lib.meter_session_channels_offset == offsetof (KirinMeterSession, channels)
        && lib.meter_session_sample_peak_offset == offsetof (KirinMeterSession, sample_peak_dbfs)
        && lib.meter_session_channel_positions_offset
               == offsetof (KirinMeterSession, channel_positions)
        && lib.meter_session_measurement_epoch_offset
               == offsetof (KirinMeterSession, measurement_epoch);
}

/** The same answer, computed once per process. The contract cannot change while it runs. */
inline bool abiMatchesLinkedLibraryOnce() noexcept
{
    static const bool matches = abiMatchesLinkedLibrary();
    return matches;
}

} // namespace kirin
