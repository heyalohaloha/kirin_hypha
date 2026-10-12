#include "../../src/live_compare/LiveCompareClock.h"
#include <cstdio>
#include <cstdlib>
using namespace hypha::live_compare;
static void require (bool ok, const char* why)
{ if (! ok) { std::fprintf (stderr, "%s\n", why); std::exit (1); } }
int main()
{
    const hypha::HostAuxiliaryClock missing { 0, hypha::AuxiliaryClockSource::vst3Continuous, false };
    ContinuousClock ordinary, reaper;
    for (int n = 0; n < 20; ++n)
    {
        require (! ordinary.next (missing, 512).valid, "uncertified VST3 remains unavailable");
        const auto reading = reaper.next (missing, 512, true);
        require (reading.valid && reading.samples == n * 512, "certified REAPER counts actual processing");
        require (reading.basis == ClockBasis::reaperProcessing, "REAPER has distinct provenance");
    }
    require (! reaper.next (missing, 512).valid, "certificate loss fences origin");
    ContinuousClock absentSource;
    require (!absentSource.next ({}, 512, true).valid, "missing source cannot become generic plugin frames");
    ContinuousClock other;
    const auto au = other.next ({ 999, hypha::AuxiliaryClockSource::audioUnitRender, true }, 512, true);
    require (au.basis == ClockBasis::audioUnitRender && au.samples == 999, "AU never becomes REAPER");
    std::puts ("REAPER counter: missing VST3, provenance, certificate loss, AU isolation PASS");
}
