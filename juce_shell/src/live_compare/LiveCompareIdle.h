#pragma once

namespace hypha::live_compare
{
// This is output routing, not permission to skip clock preparation or MATCH invalidation.
// An absent publication is inspected without dereferencing a mapping; a concurrent new
// publication simply begins its audition on the next callback. No PRE receipt is produced.
// A fade/ramp lease, END receipt, anonymous command or held attenuation must use the full path.
inline bool unchangedPostOnly (bool publishedLease, bool finishing, bool blindActive,
                               float target, float actual) noexcept
{
    return ! publishedLease && ! finishing && ! blindActive && target == 1.0f && actual == 1.0f;
}
}
