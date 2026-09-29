#include "../../src/live_compare/LiveBlindSession.h"
#include <cstdlib>
#include <iostream>

static void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}

int main()
{
    using namespace hypha::live_compare;
    BlindSession unavailable;
    require (! unavailable.startWith ([] () -> bool { throw 1; })
        && ! unavailable.command().active(), "CSPRNG failure never publishes a fallback assignment");
    for (bool firstPre : { false, true })
    {
        BlindSession trial;
        trial.start (firstPre);
        const auto first = trial.command();
        require (first.pre() == firstPre && first.stimulus() == 1, "both random mappings work");
        trial.observe (first, false);
        require (! trial.answer (1), "a crossfade alone is not a played source");
        trial.observe (first, true);
        require (! trial.answer (1), "one source is not enough");
        require (trial.select (2), "select second");
        const auto second = trial.command();
        require (second.pre() != firstPre, "second is the other source");
        trial.observe (first, true);
        require (trial.view().audible == 0, "late first receipt is not a second receipt");
        require (! trial.answer (1), "late receipt does not unlock answer");
        trial.observe (second, true);
        require (trial.view().played == 3 && trial.answer (3), "both sources permit no-preference answer");
        require (trial.view().revealed && trial.view().firstPre == firstPre, "reveal agrees with rendered mapping");
        require (! trial.answer (2), "answer only once");
        trial.invalidate (second);
        require (trial.view().invalidated && ! trial.select (1), "fault after reveal cannot restart");
        trial.start (! firstPre);
        trial.observe (second, true);
        trial.invalidate (second);
        require (trial.view().played == 0 && ! trial.view().invalidated, "old trial cannot affect new trial");
        const auto current = trial.command();
        trial.end();
        trial.observe (current, true);
        require (! trial.command().active() && ! trial.answer (1), "END defeats delayed audio receipts");
        trial.start (firstPre);
        require (! trial.select (0) && ! trial.select (3) && ! trial.answer (5), "bad commands are refused");
    }
    std::cout << "Live Blind session: PASS (both mappings, late receipts, faults, END, restart)\n";
}
