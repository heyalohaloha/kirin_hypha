#pragma once
#include "ChainTrace.h"
#include <cmath>
#include <ostream>

namespace hypha::chain_trace
{
// IO worker only. Explicit JSON fields, no native struct/padding persisted. Large identifiers
// and nanoseconds are strings so readers cannot silently round them through a JS Number.
inline void number (std::ostream& out, double value)
{ if (std::isfinite (value)) out << value; else out << "null"; }
inline void blockJson (std::ostream& out, const live_compare::BlockClock& b)
{
    out << "{\"clock\":" << b.clock << ",\"project\":" << b.project
        << ",\"frames\":" << b.frames << ",\"clockValid\":" << b.clockValid
        << ",\"projectValid\":" << b.projectValid << ",\"playing\":" << b.playing
        << ",\"gap\":" << b.afterGap << ",\"basis\":" << int (b.clockBasis)
        << ",\"authority\":" << int (b.clockAuthority) << ",\"presentationSource\":" << int (b.presentationSource)
        << ",\"outputValid\":" << b.outputPresentationValid << ",\"outputLatency\":" << b.outputPresentationSamples
        << ",\"runtimeExpired\":" << b.runtimeExpired
        << ",\"maximumDelay\":" << b.maximumDelaySamples
        << ",\"runtimeChainA\":\"" << b.runtimeChainA << "\",\"runtimeChainB\":\"" << b.runtimeChainB
        << "\",\"runtimeRevision\":\"" << b.runtimeRevision << "\",\"wallNanos\":\"" << b.wallNanos
        << "\",\"thread\":\"" << b.thread << "\",\"restart\":" << b.transportRestart
        << ",\"loopActive\":" << b.loop.active << ",\"loopValid\":" << b.loop.valid << ",\"ppq\":";
    number (out, b.loop.ppq); out << ",\"bpm\":"; number (out, b.loop.bpm);
    out << ",\"loopStart\":"; number (out, b.loop.start);
    out << ",\"loopEnd\":"; number (out, b.loop.end); out << '}';
}
inline void rowJson (std::ostream& out, const Row& r, std::uint64_t offset)
{
    out << "{\"type\":\"block\",\"serial\":" << r.serial << ",\"localFrames\":" << r.localFrames
        << ",\"entryNanos\":\"" << r.entryNanos << "\",\"exitNanos\":\"" << r.exitNanos
        << "\",\"role\":" << r.role << ",\"wrapper\":" << r.wrapper << ",\"rate\":";
    number (out, r.rate); out << ",\"precisionBits\":" << r.precisionBits;
    out << ",\"frames\":" << r.frames << ",\"channels\":" << r.channels << ",\"pcmOffset\":" << offset
        << ",\"bypassed\":" << r.bypassed << ",\"offline\":" << r.offline
        << ",\"reset\":" << r.reset << ",\"wallGap\":" << r.wallGap
        << ",\"fixtureDelay\":" << r.fixtureDelay << ",\"fixtureReported\":" << r.fixtureReported
        << ",\"fixtureGeneration\":" << r.fixtureGeneration
        << ",\"outputTaken\":" << r.outputTaken << ",\"blockSeen\":" << r.blockSeen
        << ",\"feed\":" << r.feed << ",\"active\":" << r.active << ",\"selected\":" << r.selected
        << ",\"blind\":" << r.blind << ",\"contentHeld\":" << r.contentHeld << ",\"preGain\":";
    number (out, r.preGain); out << ",\"postGain\":"; number (out, r.postGain);
    out << ",\"host\":{\"project\":" << r.host.positionSamples
        << ",\"projectValid\":" << r.host.hasPosition << ",\"clockSource\":" << int (r.host.clockSource)
        << ",\"auxiliary\":" << r.host.auxiliary.samples << ",\"auxiliaryValid\":" << r.host.auxiliary.valid
        << ",\"auxiliarySource\":" << int (r.host.auxiliary.source)
        << ",\"inputValid\":" << r.host.inputPresentationValid << ",\"inputLatency\":" << r.host.inputPresentationSamples
        << "},\"block\":";
    blockJson (out, r.block);
    out << ",\"peer\":{\"sequence\":" << r.peer.sequence << ",\"generation\":" << r.peer.generation
        << ",\"ownerA\":\"" << r.peer.ownerA << "\",\"ownerB\":\"" << r.peer.ownerB
        << "\",\"active\":" << r.peer.active << ",\"origin\":" << r.peer.origin
        << ",\"loopClock\":" << r.peer.anchor.clock << ",\"loopProject\":" << r.peer.anchor.project
        << ",\"loopSamples\":" << r.peer.anchor.loopSamples
        << ",\"intervalStart\":" << r.peer.anchor.intervals.start
        << ",\"intervalProject\":" << r.peer.anchor.intervals.project
        << ",\"intervalSplit\":" << r.peer.anchor.intervals.split
        << ",\"intervalSplitProject\":" << r.peer.anchor.intervals.splitProject
        << ",\"intervalEnd\":" << r.peer.anchor.intervals.end << ",\"block\":";
    blockJson (out, r.peer.block);
    out << "},\"timing\":{\"seen\":" << r.timingSeen << ",\"valid\":" << r.timing.valid
        << ",\"k\":" << r.timing.k << ",\"kind\":" << int (r.timing.kind)
        << ",\"failure\":" << int (r.timing.failure) << ",\"loopSamples\":" << r.timing.loopSamples
        << "},\"decision\":{\"seen\":" << r.renderSeen << ",\"verdict\":" << int (r.decision.verdict)
        << ",\"preStart\":" << r.decision.preStart << ",\"k\":" << r.decision.k
        << ",\"kValid\":" << r.decision.kValid << ",\"candidateSeen\":" << r.decision.candidateSeen
        << ",\"candidate\":" << r.decision.candidate << ",\"m1\":" << r.decision.invalidatedByDisagreement
        << ",\"timelineChanged\":" << r.decision.timelineChanged << ",\"loss\":" << int (r.decision.loss)
        << ",\"run\":" << r.decision.run << "},\"render\":{\"verdict\":" << int (r.render.verdict)
        << ",\"reason\":" << int (r.render.reason) << ",\"source\":" << int (r.render.audibleSource)
        << ",\"preAudible\":" << r.render.preAudible << ",\"preWaiting\":" << r.render.preWaiting
        << ",\"stable\":" << r.render.stableSource << ",\"gainSettled\":" << r.render.gainSettled
        << ",\"guard\":" << r.render.guardTripped << "}}\n";
}
}
