#pragma once
#include "ReferenceControllerTestAccess.h"

// Stop the ordinary worker before using this fixture. Drive its real file reads,
// capture consumption and publication synchronously, without a wall-clock race.
namespace hypha::tests::reference_control
{
struct ServiceTag {};
void serviceFor (Runtime&, ServiceTag);
struct PageServiceTag {};
void servicePages (reference_audition::AudioPages&, PageServiceTag);
template <auto Once> struct PageServiceAccess
{
    friend void servicePages (reference_audition::AudioPages& p, PageServiceTag) { (p.*Once)(); }
};
template struct PageServiceAccess<&reference_audition::AudioPages::serviceOnce>;
template <auto Requested, auto Applied, auto Apply, auto Refresh, auto Prepared, auto Pages, auto StagingPages,
          auto Deferred, auto Retirement, auto Fading> struct ManualServiceAccess
{
    friend void serviceFor (Runtime& r, ServiceTag)
    {
        const auto configuration = r.*Requested;
        if (configuration.generation != r.*Applied) (r.*Apply) (configuration);
        (r.*Deferred)();
        (r.*Retirement)();
        if (! (r.*Fading)()) (r.*Refresh) (configuration, juce::Time::currentTimeMillis());
        servicePages (r.*Pages, {});
        servicePages (r.*StagingPages, {});
        if (! (r.*Fading)()) (r.*Prepared) (configuration);
    }
};
template struct ManualServiceAccess<&Runtime::requestedConfiguration,
    &Runtime::appliedConfigurationGeneration, &Runtime::applyConfiguration,
    &Runtime::refreshWorkspace, &Runtime::servicePreparedWorkspace, &Runtime::pages, &Runtime::preparedPages,
    &Runtime::serviceDeferredAudioThreadActions, &Runtime::serviceOutputRetirement,
    &Runtime::deferredRevokeWaiting>;

struct StopTag {};
bool stopFor (Runtime&, StopTag);
template <auto Stop> struct StopAccess
{
    friend bool stopFor (Runtime& r, StopTag) { return (((juce::Thread&) r).*Stop) (-1); }
};
template struct StopAccess<&juce::Thread::stopThread>;

inline void stopWorkers (Comparison& c)
{
    for (int slot = 1; slot <= 3; ++slot)
    {
        auto& r = roleFor (c, { slot });
        if (! stopFor (r, {})) throw std::runtime_error ("Reference worker did not stop");
    }
}
inline void service (Comparison& c)
{
    for (int slot = 1; slot <= 3; ++slot) serviceFor (roleFor (c, { slot }), {});
}
}
