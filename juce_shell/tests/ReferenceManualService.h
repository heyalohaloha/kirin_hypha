#pragma once
#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceSourceRevisionService.h"

// Stop the ordinary worker before using this fixture. Drive its real file reads,
// capture consumption and publication synchronously, without a wall-clock race.
namespace hypha::tests::reference_control
{
struct PreparationClockTag {};
void elapsePreparationClocks (Runtime&, PreparationClockTag);
template <auto Hold, auto Failure> struct PreparationClockAccess
{
    friend void elapsePreparationClocks (Runtime& r, PreparationClockTag)
    {
        auto& hold = r.*Hold;
        if (hold.load() != 0) hold.store (juce::Time::getMillisecondCounter() - 21000u);
        auto& failure = r.*Failure;
        if (failure != 0) failure = juce::Time::currentTimeMillis() - 4000;
    }
};
template struct PreparationClockAccess<&Runtime::versionHoldSinceMs, &Runtime::pendingSourceFailureSinceMs>;
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
inline void probeSourceNow (const reference_audition::RuntimeSource& source, std::int64_t missingMs = 0)
{
    auto& revisions = reference_audition::revision_detail::service();
    if (!revisions.waitForIdle (std::chrono::seconds (30))) throw std::runtime_error ("source worker did not become idle");
    revisions.advanceMonitorForTest (source, missingMs);
    reference_audition::RuntimeV2SourceRepository (juce::File {}).verifySourceRevision (source);
    if (!revisions.waitForCompletion (source, std::chrono::seconds (30))) throw std::runtime_error ("source probe did not finish");
}
#endif
struct PreparedSourceTag {};
std::shared_ptr<const reference_audition::RuntimeSource> preparedSourceFor (Runtime&, PreparedSourceTag);
template <auto Member> struct PreparedSourceAccess
{
    friend std::shared_ptr<const reference_audition::RuntimeSource> preparedSourceFor (Runtime& r, PreparedSourceTag)
    { const auto& p = r.*Member; return p ? p->source : nullptr; }
};
template struct PreparedSourceAccess<&Runtime::preparedWorkspace>;
struct ServiceTag {};
void serviceFor (Runtime&, ServiceTag);
void refreshOnlyFor (Runtime&, ServiceTag);
struct PageServiceTag {};
void servicePages (reference_audition::AudioPages&, PageServiceTag);
template <auto Once> struct PageServiceAccess
{
    friend void servicePages (reference_audition::AudioPages& p, PageServiceTag) { (p.*Once)(); }
};
template struct PageServiceAccess<&reference_audition::AudioPages::serviceOnce>;
template <auto Requested, auto Applied, auto Apply, auto Refresh, auto Prepared, auto Pages, auto StagingPages,
          auto Deferred, auto Retirement, auto Fading, auto Capture> struct ManualServiceAccess
{
    friend void refreshOnlyFor (Runtime& r, ServiceTag)
    { (r.*Refresh) (r.*Requested, juce::Time::currentTimeMillis()); }
    friend void serviceFor (Runtime& r, ServiceTag)
    {
        const auto configuration = r.*Requested;
        if (configuration.generation != r.*Applied) (r.*Apply) (configuration);
        (r.*Capture) (configuration);
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
    &Runtime::deferredRevokeWaiting, &Runtime::serviceACapture>;

struct StagedLaunchTag {};
std::launch& stagedLaunchFor (Runtime&, StagedLaunchTag);
template <auto Member> struct StagedLaunchAccess
{ friend std::launch& stagedLaunchFor (Runtime& r, StagedLaunchTag) { return r.*Member; } };
template struct StagedLaunchAccess<&Runtime::stagedSearchLaunch>;
struct SourceCheckTag {};
bool waitForSourceCheck (Runtime&, SourceCheckTag);
template <auto Member> struct SourceCheckAccess
{
    friend bool waitForSourceCheck (Runtime& r, SourceCheckTag)
    {
        auto& check = (r.*Member).check;
        return check.valid() && check.wait_for (std::chrono::seconds (30)) == std::future_status::ready;
    }
};
template struct SourceCheckAccess<&Runtime::sourceVerification>;
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
        stagedLaunchFor (r, {}) = std::launch::deferred;  // 位置の探索も block ごとに同期で回す
    }
}
inline void service (Comparison& c)
{
    for (int slot = 1; slot <= 3; ++slot) serviceFor (roleFor (c, { slot }), {});
}
}
