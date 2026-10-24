#pragma once
#include "ReferenceManualService.h"

namespace hypha::tests::reference_control
{
struct CurrentPagesTag {};
reference_audition::AudioPages& currentPagesFor (Runtime&, CurrentPagesTag);
template <auto Current> struct CurrentPagesAccess
{
    friend reference_audition::AudioPages& currentPagesFor (Runtime& r, CurrentPagesTag)
    { return (r.*Current)(); }
};
template struct CurrentPagesAccess<&Runtime::currentPages>;

struct RetirePagesTag {};
bool retirePagesFor (reference_audition::AudioPages&, RetirePagesTag);
template <auto Retire> struct RetirePagesAccess
{
    friend bool retirePagesFor (reference_audition::AudioPages& p, RetirePagesTag)
    { return (p.*Retire)(); }
};
template struct RetirePagesAccess<&reference_audition::AudioPages::retirePages>;

inline void stopPageRefill (reference_audition::AudioPages& p)
{
    if (! ((juce::Thread&) p).stopThread (-1))
        throw std::runtime_error ("Reference page refill did not stop");
}
}
