#pragma once
#include "ReferenceLiveAuthorityFixture.h"
#include <sstream>

namespace hypha::tests::reference_control
{
struct AdoptionDiagnosticTag {};
const void* adoptionDiagnosticFor (Runtime&, AdoptionDiagnosticTag);
template <auto Member> struct AdoptionDiagnosticAccess
{ friend const void* adoptionDiagnosticFor (Runtime& r, AdoptionDiagnosticTag) { return &(r.*Member); } };
template struct AdoptionDiagnosticAccess<&Runtime::presetAdoptionTransport>;
struct DiagnosticPagesTag {};
const reference_audition::AudioPages* diagnosticPagesFor (Runtime&, DiagnosticPagesTag);
template <auto Member> struct DiagnosticPagesAccess
{
    friend const reference_audition::AudioPages* diagnosticPagesFor (Runtime& r, DiagnosticPagesTag)
    { return (r.*Member).load (std::memory_order_acquire); }
};
template struct DiagnosticPagesAccess<&Runtime::activePages>;
struct PageDiagnosticFacts { int empty = 0, loading = 0, ready = 0, inUse = 0; std::int64_t requested = 0; std::uint64_t generation = 0; };
struct PageDiagnosticTag {};
PageDiagnosticFacts pageDiagnosticFor (const reference_audition::AudioPages&, PageDiagnosticTag);
template <auto Pages, auto Position, auto Generation> struct PageDiagnosticAccess
{
    friend PageDiagnosticFacts pageDiagnosticFor (const reference_audition::AudioPages& p, PageDiagnosticTag)
    {
        PageDiagnosticFacts f; f.requested = (p.*Position).load(); f.generation = (p.*Generation).load();
        for (const auto& page : p.*Pages) switch (page.state.load())
        { case 0: ++f.empty; break; case 1: ++f.loading; break; case 2: ++f.ready; break; case 3: ++f.inUse; break; }
        return f;
    }
};
template struct PageDiagnosticAccess<&reference_audition::AudioPages::pages,
    &reference_audition::AudioPages::requestedPosition, &reference_audition::AudioPages::activeGeneration>;
}
namespace reference_wait_diagnostic
{
inline bool enabled = false;
inline hypha::tests::reference_control::Deadline overall;
inline std::string terms;
inline bool all (std::initializer_list<std::pair<const char*, bool>> values)
{
    std::ostringstream out; bool ok = true;
    for (const auto& value : values) { out << ' ' << value.first << '=' << value.second; ok = ok && value.second; }
    terms = out.str(); return ok;
}
inline void dump (ref::RuntimeV2Controller& controller)
{
    namespace control = hypha::tests::reference_control;
    const auto s = controller.snapshot();
    const auto* pages = control::diagnosticPagesFor (controller, {});
    std::cout << "WAIT_FACTS state=" << static_cast<int> (s.state) << " ready=" << (s.state == ref::RuntimeState::ready)
        << " buffered=" << s.auditionBuffered << " hostRate=" << s.hostSampleRateHz << " sourceRate=" << s.sourceSampleRateHz
        << " revision=" << s.manifestRevision << " selection=" << s.selectionGeneration << " requestedSelection=" << controller.requestedGeneration()
        << " preset=" << s.presetId << " candidate=" << s.candidateId << " cue=" << s.cueId
        << " loudness=" << s.sourceIntegratedLoudness << " peak=" << s.sourceMaximumTruePeakDbtp
        << " binding=" << s.aBindingAvailable << " rejection=" << s.rejectionCode << " selectionStatus=" << s.presetSelectionStatus
        << " pageOpen=" << (pages && pages->sourceOpen()) << " pageFrames=" << (pages ? pages->cachedPageFrames() : 0)
        << " pageLength=" << (pages ? pages->lengthInSamples() : 0)
        << " workerLast=" << ref::referenceTestWorkerStage (&controller)
        << " adoptionLast=" << ref::referenceTestWorkerStage (control::adoptionDiagnosticFor (controller, {}));
    if (pages)
    {
        const auto f = control::pageDiagnosticFor (*pages, {});
        std::cout << " pageWorker=" << ref::referenceTestWorkerStage (pages) << " pageRequested=" << f.requested
            << " pageGeneration=" << f.generation << " pageEmpty=" << f.empty << " pageLoading=" << f.loading
            << " pageReady=" << f.ready << " pageInUse=" << f.inUse;
    }
    printCaptureWorkerFacts (std::cout, controller); std::cout << std::endl;
}
template <class Predicate> bool wait (const char* name, ref::RuntimeV2Controller& controller, Predicate predicate)
{
    namespace control = hypha::tests::reference_control;
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = enabled ? std::min (overall, start + std::chrono::seconds (10)) : start + std::chrono::minutes (5);
    std::cout << "WAIT_ENTER name=" << name << std::endl;
    terms.clear();
    const auto forced = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_WAIT_FORCE", {}) == name;
    const bool ok = control::waitUntil (deadline, [&] {
        const bool actual = predicate();
        if (forced) terms += " forcedBarrierReleased=0";
        return actual && !forced;
    });
    std::cout << (ok ? "WAIT_DONE" : "WAIT_FAIL") << " name=" << name << " elapsedMs="
        << std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - start).count()
        << " terms=" << terms << std::endl;
    if (!ok)
    {
        dump (controller);
        if (enabled)
        {
            // The fixture owns the whole process. Static JUCE thread shutdown can
            // itself starve under Normal CPU hogs; retain evidence and end now.
            std::cerr << "WAIT_DIAGNOSTIC_EXIT name=" << name << std::endl;
            std::_Exit (1);
        }
    }
    return ok;
}
template <class Predicate> void await (const char* name, ref::RuntimeV2Controller& c, Predicate predicate)
{ require (wait (name, c, predicate), name); }
}
