#pragma once
#include "../../src/live_compare/CandidateTransfer.h"
#include "../../src/live_compare/CandidateReceiver.h"
#include <thread>
#include <future>
#include "../../src/local_blind/RtPublicationSlot.h"
#include "../../src/live_compare/CandidateReturn.h"
#include "../CandidateTransitionTest.h"

template<class Require> void verifyCandidateTransfer (Require require)
{
    verifyCandidateTransition (require);
    using hypha::live_compare::CandidateTransfer;
    CandidateTransfer state;
    require (!state.covers (0, 0, 0, 1), "an idle receipt is never comparison audio");
    require (state.begin (7, 11, 13, 17, 100), "one coherent request publishes its source and lease");
    require (!state.begin (9, 19, 23, 29, 101) && state.source.load() == 11,
             "another POST cannot overwrite a live request during initialization");
    require (state.refreshLease (7, 100) && state.leased (7, 101), "current POST renews its bounded lease");
    require (state.leased (7, 99) && !state.leased (7, 100 + CandidateTransfer::leaseNanos + 1),
             "a newer heartbeat accepts an in-flight callback, but expiry still returns");
    state.publish (7, 3, 200000, 500000, true);
    require (state.covers (7, 3, 200000, 200256) && !state.covers (8, 3, 200000, 200256)
        && !state.covers (7, 4, 200000, 200256) && !state.covers (7, 3, 199999, 200256),
        "receipt binds token, run and actual replacement range");
    require (!state.settled (7, 3, 200000, 200256, 48000)
        && state.settled (7, 3, 392000, 392256, 48000), "MATCH excludes the first four seconds");
    state.publish (7, 3, -500000, -100000, true);
    require (state.settled (7, 3, -300000, -299744, 48000),
             "MATCH settling depends on elapsed replacement, not the host clock's origin");
    require (!state.settled (7, 3, std::numeric_limits<std::int64_t>::min(), -299744, 48000),
             "settling cannot wrap the oldest representable clock");
    state.publish (7, 3, 200000, 500000, true);
    state.sequence.fetch_add (1);
    require (state.covers (7, 3, 200000, 200256), "published ranges survive an unrelated receipt update");
    state.sequence.fetch_add (1);
    state.publish (7, 3, 500000, 500000, false);
    require (state.covers (7, 3, 499000, 499256), "a hole preserves earlier input still inside B's delay");
    state.publish (7, 3, 500256, 600000, true);
    require (! state.covers (7, 3, 500000, 500256) && ! state.covers (7, 3, 499900, 500300)
        && state.covers (7, 3, 500256, 500512), "only the actual hole and its crossing are unverified");
    std::uint64_t until = 0;
    state.leaseSequence.fetch_add (1);
    require (state.readLease (7, 101, until) == CandidateTransfer::Lease::updating,
             "an in-flight heartbeat is distinct from an expired permission");
    state.leaseSequence.fetch_add (1);
    require (state.readLease (7, 101, until) == CandidateTransfer::Lease::valid,
             "one completed heartbeat restores a coherent receipt");
    state.owner.store (9);
    require (!state.refreshLease (7, 500) && !state.leased (9, 501), "old POST cannot renew a new owner");
    require (state.refreshLease (9, 500) && state.leased (9, 501), "new owner requires its own callback");
    std::thread stale ([&] { for (int i = 0; i < 10000; ++i) state.refreshLease (7, 1000); });
    for (int i = 0; i < 10000; ++i)
    {
        state.refreshLease (9, 500);
        require (!state.leased (7, 501), "racing old callback never owns the current lease");
    }
    stale.join();
    state.refreshLease (9, 0);
    require (!state.leased (9, 501), "explicit END expires substitution");
    state.replacing.store (false);
    require (state.begin (11, 31, 37, 41, 600) && state.leased (11, 601)
        && state.source.load() == 31 && state.sourceOwnerA.load() == 37 && state.sourceOwnerB.load() == 41,
        "a completed expired request can be replaced with one coherent source");
    require (!state.withdrawn (11) && !state.returnedUntouched (11), "an unused request is not returned before receiver withdrawal");
    state.acknowledgeReturn (11, {});
    require (state.withdrawn (11) && state.returnedUntouched (11), "withdrawal proves an unused request even without a clock");
    state.returns[0].sequence.fetch_add (1);
    require (!state.withdrawn (11) && !state.returnedUntouched (11), "a torn unused receipt never releases measurement");
    state.returns[0].sequence.fetch_add (1);
    state.noteOutputChange (9);
    require (state.returnedUntouched (11), "a retired request cannot change a newer unused receipt");
    state.noteOutputChange (11);
    state.acknowledgeReturn (11, {});
    require (!state.returnedUntouched (11), "a partial fade remains changed after withdrawal or receiver recreation");
    CandidateTransfer racing;
    std::atomic<unsigned> admitted { 0 };
    std::thread first ([&] { if (racing.begin (3, 31, 7, 11, 100)) ++admitted; });
    if (racing.begin (5, 51, 13, 17, 100)) ++admitted;
    first.join();
    require (admitted.load() == 1 && racing.leased (racing.owner.load(), 101),
             "simultaneous POST entries grant exactly one complete request");
    hypha::live_compare::CandidateReceiver receiver;
    const auto inFlight = receiver.completion.command();
    require (!receiver.requestReturn(), "END cannot retire a receiver from its previous block's idle state");
    const auto ending = receiver.completion.command();
    receiver.completion.observe (inFlight, true, false, 1.0f);
    require (!receiver.requestReturn(), "a callback already in flight cannot acknowledge the later END");
    receiver.completion.observe (ending, true, true, 1.0f);
    require (!receiver.requestReturn(), "a fading receiver stays published for its next callback");
    receiver.completion.observe (ending, true, false, 1.0f);
    require (receiver.requestReturn() && receiver.completion.command() == ending,
             "only the completed return permits retirement, without requesting another fade");
    // Withdrawal has to exclude a callback that arrives late, and must not manufacture
    // completion while a reader already inside the slot can still change its final output.
    hypha::local_blind::RtPublicationSlot<int> slot;
    require (slot.publish (std::make_unique<int> (1)), "publish a receiver-lifetime fixture");
    std::promise<void> entered, leave;
    auto released = leave.get_future();
    std::thread callback ([&]
    {
        slot.withRealtime ([&] (int& lastOutput)
        { entered.set_value(); released.wait(); lastOutput = 2; });
    });
    entered.get_future().wait();
    require (!slot.withdraw() && slot.control() != nullptr, "withdrawal retains an in-flight reader's storage");
    require (!slot.withRealtime ([] (int&) {}), "withdrawal rejects every later callback");
    leave.set_value(); callback.join();
    require (slot.withdraw() && *slot.control() == 2, "only reader exit makes the final output safe to acknowledge");
    slot.retire(); require (slot.collect() && !slot.hasStorage(), "withdrawn receiver storage is reclaimed off RT");
    hypha::live_compare::BlockClock finalBlock;
    finalBlock.clockValid = finalBlock.projectValid = true;
    finalBlock.clock = 1000; finalBlock.project = 900; finalBlock.frames = 256;
    state.acknowledgeReturn (11, finalBlock, 7);
    std::int64_t boundary = 0, origin = 0; std::uint64_t generation = 0;
    require (state.readReturn (11, boundary, origin, generation) && boundary == 1256
        && origin == 100 && generation == 7, "return receipt binds the final output position and clock origin");
    state.returns[0].sequence.fetch_add (1);
    require (!state.readReturn (11, boundary, origin, generation), "a torn return receipt cannot release measurement");
    state.returns[0].sequence.fetch_add (1);
    // Each request keeps its own receipt: a new owner of this PRE2 erases nobody's return.
    CandidateTransfer shared;
    require (shared.begin (21, 1, 2, 3, 100), "first POST's request");
    shared.acknowledgeReturn (21, finalBlock, 7);
    shared.released.store (21); shared.replacing.store (false);
    require (shared.begin (23, 1, 2, 3, 200) && shared.withdrawn (21) && shared.returnedUntouched (21)
        && !shared.withdrawn (23), "a later claim keeps the earlier withdrawal and starts its own");
    shared.noteOutputChange (23); shared.acknowledgeReturn (23, {});
    require (shared.readReturn (21, boundary, origin, generation) && boundary == 1256 && generation == 7
        && shared.withdrawn (23) && !shared.returnedUntouched (23), "two receipts never mix");
    shared.revokeReturn (23);
    require (!shared.withdrawn (23) && shared.withdrawn (21), "resuming one request revokes only its own receipt");
    for (std::uint64_t token = 101; token < 101 + CandidateTransfer::returnCount - 1; ++token) shared.acknowledgeReturn (token, {});
    require (shared.withdrawn (21), "a revoked entry is reused before any live receipt");
    shared.acknowledgeReturn (21, {});
    require (shared.withdrawn (21) && !shared.withdrawn (22), "a repeated receipt keeps one entry");
    shared.clearReturns();
    require (!shared.withdrawn (21), "a re-created ring holds no receipt");

    hypha::live_compare::CandidateReturnContinuity projection;
    auto downstream = finalBlock; downstream.playing = true;
    hypha::live_compare::TimingEvidence proof; proof.valid = true;
    require (projection.observe (11, downstream, proof, true), "a current clock proof establishes the drain projection");
    downstream.clock += downstream.frames; downstream.project += downstream.frames;
    ++downstream.runtimeRevision; proof.valid = false;
    require (projection.observe (11, downstream, proof, false), "withdrawing the PRE2 host slot preserves unchanged linear coordinates");
    downstream.clock += downstream.frames; downstream.project += downstream.frames + 10000;
    require (!projection.observe (11, downstream, proof, false), "a seek cannot move a stale return marker through B early");
    proof.valid = true;
    require (!projection.observe (11, downstream, proof, false), "a retired timing snapshot cannot re-establish projection after a seek");
    require (projection.observe (11, downstream, proof, true), "fresh correspondence automatically restores the drain projection");

    using namespace hypha::live_compare;
    CandidateReturnSettling settling;
    BlockClock output; output.clockValid = output.projectValid = output.playing = true; output.frames = 256;
    output.clockBasis = static_cast<std::uint8_t> (ClockBasis::reaperProcessing);
    output.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
    output.runtimeChainA = 1; output.runtimeChainB = 2; output.runtimeRevision = 1;
    const auto advance = [&] { output.clock += 256; output.project += 256; };
    require (settling.advance (11, output, 48000, true, true) == 256, "a passed return marker starts settling");
    advance(); output.runtimeChainA = output.runtimeChainB = output.runtimeRevision = 0;
    require (settling.advance (11, output, 48000, false, true) == 256, "absent chain metadata pauses the completed drain");
    advance(); output.runtimeChainA = 1; output.runtimeChainB = 2; output.runtimeRevision = 7;
    require (settling.advance (11, output, 48000, false, true) == 512, "the same fresh chain resumes across a pair-proof gap");
    advance(); output.maximumDelaySamples = 4096; output.runtimeChainA = 3;
    require (settling.advance (11, output, 48000, false, true) == 0, "a changed delay requires a new current-chain return proof");
    advance(); require (settling.advance (11, output, 48000, true, true) == 256, "new proof resumes the same return");
    advance(); output.project -= 10000;
    require (settling.advance (11, output, 48000, false, true) == 0, "a seek cannot reuse the earlier passed marker");
    advance(); require (settling.advance (11, output, 48000, true, true) == 256, "fresh proof after seek starts settling");
    advance(); require (settling.advance (11, output, 48000, true, false) == 0, "ineligible output does not settle");

    settling = {}; output = {}; output.clockValid = output.projectValid = output.playing = true; output.frames = 256;
    output.clockBasis = static_cast<std::uint8_t> (ClockBasis::reaperProcessing);
    output.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::certifiedProject);
    output.runtimeChainA = 1; output.runtimeChainB = 2; output.runtimeRevision = 1; output.maximumDelaySamples = 4096;
    for (int i = 0; i < 48; ++i)
    {
        require (settling.advance (11, output, 48000, false, true, true) == 0,
                 "withdrawal alone does not bypass current chain delay and scheduling allowance");
        advance();
    }
    require (settling.advance (11, output, 48000, false, true, true) == 256,
             "host-bounded return begins settling without either old PRE");
    advance(); output.runtimeExpired = true;
    require (settling.advance (11, output, 48000, false, true, true) == 256,
             "expired metadata cannot advance return-only settling");
    advance(); output.runtimeExpired = false; output.maximumDelaySamples += 8192; output.runtimeChainA = 3;
    require (settling.advance (11, output, 48000, false, true, true) == 0,
             "a new host delay discards the previously drained interval");
    advance(); output.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
    for (int i = 0; i < 128; ++i)
    {
        require (settling.advance (11, output, 48000, false, true, true) == 0,
                 "no host chain proof means no guessed drain bound");
        advance();
    }
    // Pro Tools documents the compensation limit, so losing both PREs still drains in a loop.
    settling = {}; output = {}; output.clockValid = output.projectValid = output.playing = true; output.frames = 512;
    output.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine);
    output.maximumDelaySamples = 16383;
    const auto step = [&] { output.clock += 512; output.project += 512; };
    for (int i = 0; i < 48; ++i)
    {
        require (settling.advance (11, output, 48000, false, true, true) == 0,
                 "the documented limit and its scheduling allowance are drained first");
        step();
    }
    require (settling.advance (11, output, 48000, false, true, true) == 512,
             "a withdrawn request settles against Pro Tools' limit without either PRE");
    step(); output.project -= 10000;
    require (settling.advance (11, output, 48000, false, true, true) == 0, "a seek restarts the bounded drain");
    // A generic host has no drainage bound, regardless of the PCM ring's size.
    settling = {}; output = {}; output.clockValid = output.projectValid = output.playing = true; output.frames = 1024;
    for (std::uint32_t frames = 0; frames <= ringCapacityFrames + 192000; frames += 1024)
    {
        require (settling.advance (11, output, 48000, false, true, true) == 0,
                 "unbounded linear return cannot complete from elapsed frames");
        output.clock += 1024; output.project += 1024;
    }
    require (settling.advance (11, output, 48000, true, true, true) == 1024,
             "a fresh returned marker starts settling without a guessed bound");
    settling = {}; output.loop.active = output.loop.valid = true;
    output.loop.start = output.loop.ppq = 0; output.loop.end = 8192.0 / 24000.0;
    output.loop.bpm = 120; output.project = 0;
    for (std::uint32_t frames = 0; frames <= ringCapacityFrames + 192000; frames += 1024)
    {
        require (settling.advance (13, output, 48000, false, true, true) == 0,
                 "an ambiguous generic loop cannot complete from elapsed frames");
        output.clock += 1024; output.project = (output.project + 1024) % 8192;
        output.loop.ppq = static_cast<double> (output.project) / 24000.0;
    }

}
