#pragma once

#include "../src/HyphaObservationEquality.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests::observation_equality_contract
{
inline void require (bool value)
{
    if (! value)
    {
        std::cerr << "Observation equality contract failed\n";
        std::exit (EXIT_FAILURE);
    }
}
template <typename T> void change (T& value)
{
    if constexpr (std::is_array_v<T>)
        change (value[std::extent_v<T> - 1]);
    else
        ++value;
}
template <size_t I, typename T> void checkMember (const T& original)
{
    auto changed = original;
    // key() deliberately exposes const references in product code. The test changes the
    // non-const object it owns, one semantic member at a time (including the last array item).
    auto values = observation_equality::key (changed);
    using Member = std::remove_const_t<std::remove_reference_t<decltype (std::get<I> (values))>>;
    change (const_cast<Member&> (std::get<I> (values)));
    require (! observation_equality::same (original, changed));
}
template <typename T, size_t... I> void checkMembers (std::index_sequence<I...>)
{
    const T original {};
    require (observation_equality::same (original, original));
    (checkMember<I> (original), ...);
}
template <typename T> void checkMembers()
{
    checkMembers<T> (std::make_index_sequence<std::tuple_size_v<decltype (
        observation_equality::key (T {}))>> {});
}
inline void verify()
{
    using observation_equality::same;
    using observation_equality::field;
    require (field (0.0, -0.0));
    require (! field (1.0, std::nextafter (1.0, 2.0)));
    require (! field (std::nan (""), 0.0));
    require (! field (std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity()));
    checkMembers<KirinMeasureResult>();
    checkMembers<KirinMeterSession>();
    checkMembers<KirinDelta>();
    checkMembers<KirinMeterHistoryRange>();
    KirinObservatoryFrame a {}, b {};
    a.meter.lufs_m = b.meter.lufs_m = std::numeric_limits<double>::quiet_NaN();
    a.delta.psb_bark[19] = std::nan ("1"); b.delta.psb_bark[19] = std::nan ("2");
    require (same (a, b));
    b.reserved = 255; b.meter.reserved[2] = 3; b.meter.field_reserved[5] = 7;
    require (same (a, b));
    b.signal_state = 1; require (! same (a, b)); b.signal_state = 0;
    b.lra_state = 1; require (! same (a, b)); b.lra_state = 0;
    b.delta_available = 1; require (! same (a, b)); b.delta_available = 0;
    b.version = 1; require (! same (a, b)); b.version = 0;
    b.lra_elapsed_seconds = 1; require (! same (a, b)); b.lra_elapsed_seconds = 0;
    b.meter.lufs_m = -20; require (! same (a, b));
    a.meter.lufs_m = -20; require (same (a, b));
    a.meter.clip_events[0] = b.meter.clip_events[0] = std::uint64_t { 1 } << 54;
    ++b.meter.clip_events[0]; require (! same (a, b));
    b.meter.clip_events[0] = a.meter.clip_events[0]; require (same (a, b));
    b.meter.channel_clip_latched[1] = 1; require (! same (a, b));
    KirinWatchDisplay w {}, x {};
    w.current.lufs_s = x.current.lufs_s = -std::numeric_limits<double>::infinity();
    require (same (w, x));
    x.maximum.dropped_samples = 1; require (! same (w, x));
    KirinMeterHistoryEntry h {}, j {};
    h.plr.mean = j.plr.mean = std::nan (""); require (same (h, j));
    j.reserved = 1; require (same (h, j));
    j.plr.max = 3; require (! same (h, j)); j.plr.max = 0;
    j.first_timeline_endpoint_samples = -1; require (! same (h, j));
    j.first_timeline_endpoint_samples = 0;
    j.clip_event_count[1] = 1; require (! same (h, j));
    std::cout << "Observation equality: PASS (all scalar/array keys, NaN, ABI padding, exact clocks)\n";
}
}
