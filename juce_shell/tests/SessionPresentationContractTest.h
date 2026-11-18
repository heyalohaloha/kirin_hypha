#pragma once
#include "../src/HyphaSessionPresentation.h"
#include <limits>
#include <iostream>
namespace hypha::tests
{
template<class Require> void verifySessionPresentationContract (Require require)
{
    using namespace session_presentation;
    int count = 0;
    const auto check = [&](bool ok, const char* message) { ++count; require(ok, message); };
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    auto& m = frame.meter;
    m.generation = 2; m.measurement_epoch = 3; m.sample_rate = 48000;
    m.channels = 2; m.layout_id = 1; m.state = KIRIN_METER_SESSION_ACTIVE;
    m.active_frames = m.observed_frames = 4800;
    KirinMeterSessionV2 coverage {};
    coverage.version = 2; coverage.struct_size = sizeof(coverage);
    coverage.session = m; coverage.processed_frames = m.active_frames;
    coverage.summary_status = KIRIN_SESSION_SUMMARY_COMPLETE;
    const Input complete {&frame, &coverage, KIRIN_SNAPSHOT_SUCCESS, true};
    const Input busy {&frame, nullptr, KIRIN_SNAPSHOT_BUSY, true};
    Gate gate;
    check(gate.decide(busy, 0) == Decision::invalidate, "first BUSY cannot invent an accepted packet");
    check(gate.decide(complete, 0) == Decision::accept, "complete packet accepted");
    check(gate.decide(busy, 100) == Decision::retain, "same ACTIVE BUSY retains whole packet");
    check(gate.decide(busy, 999) == Decision::retain, "999 ms remains eligible");
    check(gate.decide(busy, 1000) == Decision::invalidate, "exactly 1000 ms expires");
    check(gate.decide(busy, 1001) == Decision::invalidate, "BUSY cannot restart expired interval");
    check(gate.decide(complete, 1100) == Decision::accept, "new coherent success rearms");
    gate.invalidate();
    check(gate.decide(busy, 1101) == Decision::invalidate, "RESET invalidation is immediate");
    const auto boundary = [&](auto mutate, const char* message) {
        Gate fresh; fresh.decide(complete, 0);
        auto changed = frame; mutate(changed);
        check(fresh.decide({&changed,nullptr,KIRIN_SNAPSHOT_BUSY,true},1) == Decision::invalidate,message);
        check(fresh.decide(busy,2) == Decision::invalidate,"boundary cannot rearm via BUSY");
    };
    boundary([](auto& f){++f.comparison_identity;},"comparison binding boundary");
    boundary([](auto& f){f.meter_stalled=1;},"stalled meter is not retained LIVE");
    boundary([](auto& f){++f.keep_first_pass_only;},"Keep progress fact changes immediately");
    boundary([](auto& f){++f.comparison_state;},"comparison availability changes immediately");
    boundary([](auto& f){++f.comparison_reason;},"comparison reason changes immediately");
    boundary([](auto& f){++f.delta_available;},"delta validity changes immediately");
    boundary([](auto& f){++f.meter.generation;},"generation boundary");
    boundary([](auto& f){++f.meter.measurement_epoch;},"epoch boundary");
    boundary([](auto& f){f.meter.sample_rate=44100;},"sample rate boundary");
    boundary([](auto& f){f.meter.channels=1;},"channels boundary");
    boundary([](auto& f){++f.meter.layout_id;},"layout boundary");
    boundary([](auto& f){++f.meter.channel_positions[0];},"channel positions boundary");
    boundary([](auto& f){f.meter.state=KIRIN_METER_SESSION_PAUSED;},"PAUSED boundary");
    boundary([](auto& f){f.meter.state=KIRIN_METER_SESSION_EMPTY;},"EMPTY boundary");
    boundary([](auto& f){f.signal_state=0;},"non-ACTIVE signal boundary");
    boundary([](auto& f){--f.meter.active_frames;},"active frame reversal");
    boundary([](auto& f){--f.meter.observed_frames;},"observed frame reversal");
    auto stalled = frame; stalled.meter_stalled = 1;
    Gate stalledGate;
    check(stalledGate.decide({&stalled,&coverage,KIRIN_SNAPSHOT_SUCCESS,true},0)==Decision::accept
          && !stalledGate.retainingEligible(),"stalled successful observation cannot arm hold");
    Gate pendingGate; pendingGate.decide(complete,0);
    auto pending = coverage; pending.summary_status=KIRIN_SESSION_SUMMARY_PENDING_TAIL;
    pending.pending_frames=479;
    check(pendingGate.decide({&frame,&pending,KIRIN_SNAPSHOT_SUCCESS,true},1)==Decision::accept
          && !pendingGate.retainingEligible(),"real pending replaces packet without arming");
    check(pendingGate.decide(busy,2)==Decision::invalidate,"pending cannot become retained complete");
    Gate progress; progress.decide(complete,0);
    auto forward = frame; forward.meter.active_frames += 10; forward.meter.observed_frames += 10;
    check(progress.decide({&forward,nullptr,KIRIN_SNAPSHOT_BUSY,true},1)==Decision::retain,"forward BUSY retains");
    check(progress.decide(busy,2)==Decision::invalidate,"BUSY reversal relative to latest observation rejected");
    auto newer = frame; ++newer.meter.active_frames;
    Gate mismatch; mismatch.decide(complete,0);
    check(mismatch.decide({&newer,&coverage,KIRIN_SNAPSHOT_SUCCESS,true},1)==Decision::invalidate,
          "old coverage must not certify new prefix");
    auto tail = coverage; tail.session.active_frames += 2; tail.session.observed_frames += 2; tail.processed_frames += 2;
    check(mismatch.decide({&newer,&tail,KIRIN_SNAPSHOT_SUCCESS,true},2)==Decision::accept,
          "newer complete 10 ms coverage is allowed");
    for (unsigned status : {KIRIN_SNAPSHOT_INVALID_REQUEST,KIRIN_SNAPSHOT_UNSUPPORTED,KIRIN_SNAPSHOT_RETIRED}) {
        Gate g; g.decide(complete,0);
        check(g.decide({&frame,nullptr,status,true},1)==Decision::invalidate,"non-BUSY failure does not hold");
    }
    Gate g; g.decide(complete,0);
    check(g.decide({nullptr,nullptr,KIRIN_SNAPSHOT_BUSY,false},1)==Decision::invalidate,
          "unobserved latest ACTIVE conservatively invalidates");
    check(g.decide({&frame,&coverage,KIRIN_SNAPSHOT_SUCCESS,false},2)==Decision::invalidate,
          "fallback frame cannot rearm a missing LEVEL packet");
    g.decide(complete,10);
    check(g.decide(busy,9)==Decision::invalidate,"backward clock invalidates");
    g.decide(complete,10);
    check(g.decide(busy,std::numeric_limits<double>::quiet_NaN())==Decision::invalidate,"nonfinite clock invalidates");
    auto invalid = coverage; invalid.version=1;
    check(g.decide({&frame,&invalid,KIRIN_SNAPSHOT_SUCCESS,true},20)==Decision::invalidate,"invalid V2 rejected");
    std::cout << "SESSION_PRESENTATION_ASSERTIONS=" << count << '\n';
}
}
