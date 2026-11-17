#include "reference_audition/ReferenceMetricSeries.h"
#include "kirin_hypha_reference_live_metrics.h"
#include <iostream>
#include <stdexcept>
using namespace hypha::reference_audition::metrics;
void require (bool ok, const char* reason) { if (!ok) throw std::runtime_error (reason); }
int main()
{
    try
    {
        const std::vector<KirinReferenceMetricFrame> frames {
            { 4800, 3.0, 8.0, 1.0 }, { 9600, 5.0, 12.0, 2.0 },
            { 14400, missing, missing, missing }, { 19200, 9.0, 20.0, 4.0 }
        };
        const auto even = summarise (frames, Metric::crest, Cue { 0, 9600 });
        require (even.observations == 2 && even.median == 4.0, "even Cue median");
        require (!even.complete, "partial is the default");
        require (summarise (frames, Metric::crest, Cue {0,9600}, true).complete, "completed scan flag retained");
        require (summarise (frames, Metric::crest, Cue {9600,14400}, true).complete, "completed missing observations stay complete but unavailable");
        const auto odd = summarise (frames, Metric::psr, Cue { 0, 19200 });
        require (odd.observations == 3 && odd.median == 12.0, "missing is not zero");
        const auto sharp = summarise (frames, Metric::sharp, Cue { 4800, 9600 });
        require (sharp.observations == 1 && sharp.median == 2.0, "Cue boundary and unit");
        require (!std::isfinite (summarise (frames, Metric::sharp, Cue { 9600, 14400 }).median), "all missing Cue");
        require (!std::isfinite (summarise (frames, Metric::psr, Cue { 19200, 0 }).median), "invalid Cue");
        const auto wrap = summarise (frames, Metric::crest, std::vector<Cue> {{ 14400, 19200 }, { 0, 4800 }});
        require (wrap.observations == 2 && wrap.median == 6.0, "wrapped Cue combines observations not medians");
        const auto overlap = summarise (frames, Metric::crest, std::vector<Cue> {{0,9600}, {4800,19200}});
        require (overlap.observations == 3 && overlap.median == 5.0, "overlapping regions not double counted");
        require (atPosition (frames, Metric::sharp, 9700, 24000, 4800) == 2.0, "latest complete aperture");
        require (!std::isfinite (atPosition (frames, Metric::psr, 14500, 24000, 4800)), "no last finite reconstruction");
        require (!std::isfinite (atPosition (frames, Metric::psr, 24000, 24000, 4800)), "no EOF extrapolation");
        require (!std::isfinite (atPosition (frames, Metric::psr, 4700, 24000, 4800)), "no fabricated first aperture");
        require (!std::isfinite (atPosition (frames, Metric::psr, 9700, 24000, 0)), "invalid aperture");
        require (!std::isfinite (setBand ({Summary {2,1,true}, std::nullopt}).low), "incomplete set has no band");
        require (!std::isfinite (setBand ({Summary {2,1,true}, Summary {missing,0,true}}).low), "missing member has no band");
        require (!std::isfinite (setBand ({Summary {2,1,true}}).low), "single song is not a band");
        require (!std::isfinite (setBand ({Summary {2,1,true}, Summary {3,1,false}}).low), "partial scan never completes set band");
        const auto band = setBand ({Summary {2,1,true}, Summary {4,100,true}, Summary {3,2,true}});
        require (band.low == 2.0 && band.high == 4.0, "set band weights songs not aperture counts");
        std::cout << "Reference metric series contracts passed\n";
        return 0;
    }
    catch (const std::exception& failure)
    { std::cerr << failure.what() << '\n'; return 1; }
}
