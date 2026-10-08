#pragma once
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaTimeSnapshotPainter.h"
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyTimeSnapshotMainTargetContract (KirinTimeSnapshotV2 packet,
    const std::vector<KirinTimeHistoryEntryV2>& main, const std::vector<KirinTimeHistoryEntryV2>& psr)
{
    const auto check = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "TIME main target contract failed: " << message << '\n'; std::exit (1); } };
    using time_snapshot::Metric;
    observatory::View view (observatory::Role::post);
    view.setSize (375, 250);
    view.setDomain (observatory::Domain::time);
    for (auto* component : { &packet.main, &packet.psr })
    {
        component->current.completion_age_ms = 0.0;
        component->current.remaining_ms = 400.0;
    }
    check (view.setTimeSnapshot (packet, main, psr, 1'000, 1'000, true), "adopt original completed slot");
    const auto previousPsr = view.acceptedTimePresentation().psr();
    const auto snapshotRevision = view.capturePresentationStamp().snapshotRevision;
    auto presentationRevision = view.capturePresentationStamp().presentationRevision;
    for (const auto target : { observatory::ObservationTarget::delta, observatory::ObservationTarget::absolute })
    {
        view.setTarget (target); // No successful poll follows: the acquisition is still BUSY.
        const auto& accepted = view.acceptedTimePresentation();
        const auto wanted = target == observatory::ObservationTarget::delta ? KIRIN_TIME_DELTA : KIRIN_TIME_POST;
        check (accepted.main().facts.current.target == wanted
            && accepted.main().facts.current.state == KIRIN_TIME_CURRENT_WAITING
            && ! accepted.main().currentAvailable (Metric::shortTerm) && accepted.main().history.empty(),
            "POST to delta to POST immediately blanks only main and names the selected target");
        check (std::memcmp (&accepted.psr().facts, &previousPsr.facts, sizeof (previousPsr.facts)) == 0
            && accepted.psr().history.size() == psr.size()
            && std::memcmp (accepted.psr().history.data(), psr.data(), psr.size() * sizeof (psr.front())) == 0
            && std::abs (accepted.psr().deadlineMs - 1'400.0) < 1.0e-12,
            "BUSY target transitions preserve PSR source, proof, values, history and original deadline");
        const auto before = view.capturePresentationStamp();
        text_style::ShownTextLog log;
        const auto frozen = view.createCaptureImage (1200, 630, false, "frozen", "G2 TARGET");
        const auto after = view.capturePresentationStamp();
        check (frozen.isValid() && before.snapshotRevision == snapshotRevision && before.requiresTypedMetadata
            && before.presentationRevision == ++presentationRevision
            && before.presentationRevision == after.presentationRevision
            && log.texts().contains ("S ---") && log.texts().contains (juce::String::fromUTF8 ("PSR Δ −3.3 dB")),
            "frozen Capture uses waiting main and retained PSR without adoption or recalculation");
    }
    view.advanceTimePresentation (1'399);
    check (view.acceptedTimePresentation().psr().currentAvailable (Metric::psr), "original 400 ms slot remains live before expiry");
    view.advanceTimePresentation (1'400);
    check (! view.acceptedTimePresentation().psr().currentAvailable (Metric::psr)
        && view.acceptedTimePresentation().psr().history.size() == psr.size(), "target toggles never extend original expiry");
    text_style::ShownTextLog expired;
    check (view.createCaptureImage (1200, 630, false, "frozen", "G2 TARGET").isValid()
        && expired.texts().contains (juce::String::fromUTF8 ("PSR Δ --- dB")), "Capture preserves original expired state");
    time_snapshot::Presentation stopped;
    check (stopped.apply (packet, main, psr, 1'000, 1'000, true), "adopt stop-fence canary");
    stopped.observeInput (false);
    stopped.selectMainTarget (KIRIN_TIME_DELTA);
    stopped.observeInput (true);
    check (stopped.apply (packet, main, psr, 1'050, 1'050, true)
        && ! stopped.main().currentAvailable (Metric::shortTerm) && ! stopped.psr().currentAvailable (Metric::psr),
        "main target selection retains the local completed-slot stop fence");
}

inline void verifyTimeSnapshotInputContract (KirinTimeSnapshotV2 packet,
    const std::vector<KirinTimeHistoryEntryV2>& main, const std::vector<KirinTimeHistoryEntryV2>& psr)
{
    const auto check = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "TIME input stop contract failed: " << message << '\n'; std::exit (1); } };
    using time_snapshot::Metric;
    for (int route = 0; route < 3; ++route)
    {
        observatory::View view (observatory::Role::post);
        view.setSize (375, 250);
        view.setDomain (observatory::Domain::time);
        KirinObservatoryFrame frame {};
        frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
        frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
        frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
        frame.meter.generation = packet.post_span.generation;
        frame.meter.measurement_epoch = packet.post_span.epoch;
        frame.meter.sample_rate = packet.post_span.sample_rate;
        frame.meter.channels = packet.post_span.channels;
        frame.meter.observed_frames = frame.meter.active_frames = packet.local_cutoff;
        view.setObservatoryFrame (frame, true);
        check (view.setTimeSnapshot (packet, main, psr, 1'000, 1'080, true), "adopt active packet");
        auto stopped = frame;
        stopped.signal_state = route == 1 ? KIRIN_SIGNAL_STATE_BYPASSED : KIRIN_SIGNAL_STATE_INACTIVE;
        stopped.meter.state = KIRIN_METER_SESSION_PAUSED;
        if (route == 0) view.setObservatoryFrame (stopped, true);
        else if (route == 1) view.setLevelObservation (nullptr, {}, nullptr, &stopped);
        else view.setMeterSnapshot (stopped.meter, true);
        const auto& adopted = view.acceptedTimePresentation();
        check (! adopted.main().currentAvailable (Metric::shortTerm)
            && ! adopted.psr().currentAvailable (Metric::psr)
            && adopted.main().facts.current.state == KIRIN_TIME_CURRENT_STOPPED
            && adopted.psr().facts.current.state == KIRIN_TIME_CURRENT_STOPPED,
            "direct, fallback and compatibility paths stop both current components immediately");
        check (adopted.packet().local_cutoff == 480'000 && adopted.psr().facts.current.cutoff == 470'400
            && adopted.main().history.size() == 2u && adopted.psr().history.size() == 2u
            && adopted.main().deadlineMs == 1'100.0 && adopted.psr().deadlineMs == 1'100.0,
            "confirmed pause/bypass retains histories, C/E and original completion deadlines");
        view.setLevelObservation (nullptr, {}, nullptr, &stopped);
        check (view.setTimeSnapshot (packet, main, psr, 1'085, 1'085, true)
            && ! adopted.main().currentAvailable (Metric::shortTerm),
            "same-state fallback and a packet arriving while inactive cannot revive current");
        view.setObservatoryFrame (frame, true);
        auto late = packet;
        ++late.revision;
        ++late.psr.claim_identity;
        check (view.setTimeSnapshot (late, main, psr, 1'090, 1'090, true)
            && ! adopted.main().currentAvailable (Metric::shortTerm)
            && ! adopted.psr().currentAvailable (Metric::psr),
            "Active resume and a changed PRE proof at old C cannot resurrect pre-stop current");
        auto fresh = late;
        fresh.local_cutoff = fresh.main.current.cutoff = fresh.psr.current.cutoff = 484'800;
        check (view.setTimeSnapshot (fresh, main, psr, 1'095, 1'095, true)
            && adopted.main().currentAvailable (Metric::shortTerm)
            && adopted.psr().currentAvailable (Metric::psr),
            "only current observations beyond the stopped local C resume in the same source");
    }

    time_snapshot::Presentation isolated;
    check (! isolated.observeInput (false), "inactive before a packet needs no synthetic presentation");
    check (isolated.apply (packet, main, psr, 1'000, 1'000, true)
        && ! isolated.main().currentAvailable (Metric::shortTerm),
        "a first packet remains stopped while input is known inactive");
    isolated.retire (true);
    check (isolated.apply (packet, main, psr, 1'020, 1'020, true)
        && ! isolated.main().currentAvailable (Metric::shortTerm),
        "source retirement preserves confirmed inactive input");
    isolated.observeInput (true);
    check (isolated.apply (packet, main, psr, 1'030, 1'030, true)
        && ! isolated.main().currentAvailable (Metric::shortTerm),
        "retirement and resume retain the old-source cutoff fence");
    ++packet.post_span.incarnation;
    packet.main.current.span = packet.psr.current.span = packet.post_span;
    check (isolated.apply (packet, main, psr, 1'040, 1'040, true)
        && isolated.main().currentAvailable (Metric::shortTerm)
        && isolated.psr().currentAvailable (Metric::psr),
        "a genuinely new source may resume after Active without inheriting the old fence");
}
}
