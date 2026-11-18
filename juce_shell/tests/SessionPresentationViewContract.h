#pragma once
#include "../src/HyphaObservatoryView.h"
namespace hypha::tests
{
template<class Require> void verifySessionPresentationViewContract (Require require)
{
    int count = 0;
    const auto check = [&](bool ok, const char* message) { ++count; require (ok, message); };
    observatory::View view (observatory::Role::post);
    view.setSize (600, 400);
    view.setDomain(observatory::Domain::level);
    view.setTarget(observatory::ObservationTarget::absolute);
    view.setTimeRange(observatory::TimeRange::seconds30);
    view.setAnalysisPage(analysis_navigation::Page::meters);
    KirinLevelSnapshot a {};
    a.version = KIRIN_LEVEL_SNAPSHOT_VERSION;
    a.frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    a.frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    auto& meter = a.frame.meter;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.generation = 7; meter.measurement_epoch = 19;
    meter.sample_rate = 48000; meter.channels = 2;
    meter.active_frames = meter.observed_frames = 4800;
    meter.lufs_i = -20; meter.max_true_peak = -3; meter.plr = 17;
    a.chain_updated = 1; a.chain.version = KIRIN_CHAIN_VERSION; a.chain.revision = 42;
    a.history_count = 1; a.chain.count = 1;
    KirinMeterSessionV2 coverage {};
    coverage.version = 2; coverage.struct_size = sizeof (coverage);
    coverage.session = meter; coverage.processed_frames = meter.active_frames;
    coverage.summary_status = KIRIN_SESSION_SUMMARY_COMPLETE;
    const auto apply = [&] (const KirinLevelSnapshot& packet, const KirinMeterSessionV2* value,
                            unsigned status, double now)
    {
        if (view.retainSessionPoll (&packet.frame, value, status, true, now)) return true;
        KirinMeterHistoryEntry history {};
        history.measurement_epoch = packet.frame.meter.measurement_epoch;
        history.generation = packet.frame.meter.generation; history.run_id = 1;
        history.first_observed_frames = history.last_observed_frames = packet.frame.meter.observed_frames;
        history.observation_count = 1;
        history.lufs_m = {packet.frame.meter.lufs_i,packet.frame.meter.lufs_i,packet.frame.meter.lufs_i};
        KirinChainPoint point {};
        point.post_epoch = packet.frame.meter.measurement_epoch;
        point.post_generation = packet.frame.meter.generation;
        view.setLevelObservation (&packet, {history}, &point);
        KirinWatchDisplay watch {};
        watch.current.lufs_m = packet.frame.meter.lufs_i;
        watch.maximum.lufs_m = packet.frame.meter.lufs_i + 1;
        view.setWatchDisplay(watch,true);
        return false;
    };
    check (! apply (a, &coverage, KIRIN_SNAPSHOT_SUCCESS, 0), "initial whole packet accepted");
    check (! view.sessionSummaryPending(), "initial coverage confirmed");
    const auto packetValues = [&](std::uint64_t endpoint,double value,const char* message) {
        const auto& history = view.sessionHistoryForTest();
        check(history.size()==1 && history.front().last_observed_frames==endpoint
              && history.front().lufs_m.mean==value
              && view.sessionWatchForTest().current.lufs_m==value
              && view.sessionWatchForTest().maximum.lufs_m==value+1,message);
    };
    packetValues(4800,-20,"initial nonempty history and Watch accepted");
    auto b = a;
    b.frame.meter.active_frames = b.frame.meter.observed_frames = 9600;
    b.frame.meter.lufs_i = -8; b.chain.revision = 43;
    check (apply (b, nullptr, KIRIN_SNAPSHOT_BUSY, 999), "view retains at 999 ms");
    check (view.frameForPoll().meter.active_frames == 4800
             && view.cumulativeMeterForDisplay().lufs_i == -20
             && view.captureHistoryEndpoint() == 4800 && view.levelChainRevision (false) == 42
             && ! view.capturePresentationStamp().requiresTypedMetadata,
             "frame summary chain and capture stay on the same accepted packet");
    packetValues(4800,-20,"BUSY retains nonempty history and both Watch values");
    check (! apply (b, nullptr, KIRIN_SNAPSHOT_BUSY, 1000), "view expires at 1000 ms");
    check (view.frameForPoll().meter.active_frames == 9600
             && view.cumulativeMeterForDisplay().lufs_i == -8
             && view.levelChainRevision (false) == 43 && view.sessionSummaryPending()
             && view.capturePresentationStamp().requiresTypedMetadata,
             "expiry admits new prefix without old coverage certification");
    packetValues(9600,-8,"expiry adopts new nonempty history and both Watch values");
    coverage.session = b.frame.meter; coverage.processed_frames = 9600;
    check (! apply (b, &coverage, KIRIN_SNAPSHOT_SUCCESS, 1100), "fresh complete packet rearms");
    view.invalidateSessionPresentation();
    check (view.sessionSummaryPending()
             && ! apply (b, nullptr, KIRIN_SNAPSHOT_BUSY, 1101), "reset immediately clears confirmation and hold");
    apply (b, &coverage, KIRIN_SNAPSHOT_SUCCESS, 1200);
    auto pending = coverage; pending.summary_status = KIRIN_SESSION_SUMMARY_PENDING_TAIL;
    pending.pending_frames = 479;
    check (! apply (b, &pending, KIRIN_SNAPSHOT_SUCCESS, 1201) && view.sessionSummaryPending(),
             "real pending is not hidden by previous complete packet");
    const auto scopeBoundary = [&](auto change,const char* message) {
        apply(b,&coverage,KIRIN_SNAPSHOT_SUCCESS,2000);
        change();
        check(!apply(b,nullptr,KIRIN_SNAPSHOT_BUSY,2001),message);
        check(view.sessionSummaryPending(),"scope change cannot carry old coverage certification");
    };
    scopeBoundary([&]{view.setDomain(observatory::Domain::time);},"domain change disarms hold");
    scopeBoundary([&]{view.setTarget(observatory::ObservationTarget::delta);},"target change disarms hold");
    scopeBoundary([&]{view.setTimeRange(observatory::TimeRange::minutes2);},"range change disarms hold");
    scopeBoundary([&]{view.setAnalysisPage(analysis_navigation::Page::run);},"page change disarms hold");
    std::cout << "SESSION_PRESENTATION_VIEW_ASSERTIONS=" << count << '\n';
}
}
