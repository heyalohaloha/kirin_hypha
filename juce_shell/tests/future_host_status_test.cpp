#include "kirin_hypha_ffi.h"
// Exercise the actual additive ABI values without redefining any public-header macro.
#include "../src/HyphaHostAvailability.h"
#include "../src/HyphaComparisonPresentation.h"
#include <iostream>
int main()
{
    using namespace hypha;
    for (unsigned value = 0; value < 256; ++value)
        if (host_availability::outputLatencyUnavailable (static_cast<std::uint8_t> (value)) != (value == 6)
            || comparison_presentation::statusText (KIRIN_COMPARISON_STATE_REJECTED,
                static_cast<std::uint8_t> (value)).contains ("POST BYPASSED") != (value == 12))
        { std::cerr << "Host ABI unavailable/bypass states must match only 6 and 12\n"; return 1; }
    if (! host_availability::outputLatencyUnavailable (6)
        || host_availability::outputLatencyUnavailable (KIRIN_SPECTRUM_WARMING_UP)
        || ! comparison_presentation::statusText (KIRIN_COMPARISON_STATE_REJECTED, 12).contains ("POST BYPASSED")
        || ! comparison_presentation::spectrumStatusText (KIRIN_COMPARISON_STATE_REJECTED, 12).contains ("POST BYPASSED")
        || comparison_presentation::notifiesExplicitAction (KIRIN_COMPARISON_STATE_REJECTED, 12)
        || comparison_presentation::notifiesExplicitAction (KIRIN_COMPARISON_STATE_POST_ABSOLUTE, 12)
        || ! comparison_presentation::notifiesExplicitAction (KIRIN_COMPARISON_STATE_REJECTED,
                                                             KIRIN_COMPARISON_REASON_NO_PAIR))
    { std::cerr << "Optional host ABI states must have distinct unavailable explanations\n"; return 1; }
    std::cout << "Future host ABI status: PASS 256 values + explicit-action boundaries\n";
}
