#pragma once
#include "ReferenceSongSwitchRig.h"
#include <algorithm>
#include <iomanip>
#include <iostream>

// KIRIN_REFERENCE_SWITCH_BENCHMARK_PHASE=before|after で、5 分の 24 bit の曲の切替を実時間の block で測る。
// before は旧曲を保たず先に A へ戻す道と 50 ms の周期（先読みなし）。after は一組の切替。
namespace reference_switch_benchmark
{
using namespace reference_switch_rig;
namespace revision = hypha::reference_audition::revision_detail;
struct Sample { double firstMs = 0.0, doneMs = 0.0; int outside = 0; std::int64_t readBytes = 0, copies = 0; };

inline Sample measure (Rig& rig, int from, int to, int passThrough = -1)
{
    Sample sample;
    const auto reads = revision::snapshotReadBytesForTest.load();
    const auto copies = revision::snapshotCopiesForTest.load();
    const auto before = songLevel (from), target = songLevel (to);
    const auto pressed = Clock::now();
    if (passThrough >= 0) { require (rig.press (passThrough), "benchmark presses the passing song"); rig.host(); }
    require (rig.press (to), "benchmark presses the song");
    bool first = false;
    const auto ms = [&] { return std::chrono::duration<double, std::milli> (Clock::now() - pressed).count(); };
    rig.drive ([&] {
        for (int index = 0; index < rig.block.getNumSamples(); ++index)
        {
            const auto left = rig.block.getSample (0, index);
            if (left < std::min (before.left, target.left) - 1.0e-3) ++sample.outside;
            if (! first && left > before.left + 1.0e-3) { first = true; sample.firstMs = ms(); }
        }
        const bool done = rig.blockIs (target);
        if (done) sample.doneMs = ms();
        return done;
    }, "benchmark switch completes");
    if (! first) sample.firstMs = sample.doneMs;
    sample.readBytes = revision::snapshotReadBytesForTest.load() - reads;
    sample.copies = static_cast<std::int64_t> (revision::snapshotCopiesForTest.load()) - copies;
    return sample;
}

inline void report (const char* name, const juce::String& phase, std::vector<Sample> samples)
{
    const auto at = [] (std::vector<double> values, double p) {
        std::sort (values.begin(), values.end());
        return values[static_cast<size_t> (std::lround ((values.size() - 1) * p))];
    };
    std::vector<double> first, done, outside, read, copies;
    for (const auto& sample : samples)
    {
        first.push_back (sample.firstMs); done.push_back (sample.doneMs); outside.push_back (sample.outside);
        read.push_back (static_cast<double> (sample.readBytes) / (1024.0 * 1024.0)); copies.push_back (static_cast<double> (sample.copies));
    }
    std::cout << std::fixed << std::setprecision (1) << phase << "\t" << name << "\tn=" << samples.size()
              << "\tnew sample ms median/p95/max " << at (first, 0.5) << "/" << at (first, 0.95) << "/" << at (first, 1.0)
              << "\tdone ms " << at (done, 0.5) << "/" << at (done, 0.95) << "/" << at (done, 1.0)
              << "\tframes through A median/max " << at (outside, 0.5) << "/" << at (outside, 1.0)
              << "\tMiB read median " << at (read, 0.5) << "\tcopies median " << at (copies, 0.5) << std::endl;
}

inline void run (const juce::File& sandbox, const juce::String& phase)
{
    const bool after = phase == "after";
    const auto repetitions = juce::jmax (3, juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_SWITCH_BENCHMARK_REPS", "10").getIntValue());
    const auto root = sandbox.getChildFile ("b-switch-benchmark");
    reference_switch_fixture::createLibrary (root, true);
    const auto makeRig = [&] (std::unique_ptr<Rig>& rig, int index)
    {
        rig.reset();
        rig = std::make_unique<Rig> (root, ("b-benchmark-" + juce::String (index)).toRawUTF8());
        pairedSwitchFor (rig->bRuntime(), {}).store (after);
        prepareFirstFor (rig->bRuntime(), {}).store (after);
        rig->start();
    };
    std::vector<Sample> firstPress, revisit, preloaded, rapid;
    for (int index = 0; index < repetitions; ++index)
    {
        std::unique_ptr<Rig> rig;
        makeRig (rig, index);
        firstPress.push_back (measure (*rig, 0, 1));
        revisit.push_back (measure (*rig, 1, 0));
        if (! after) continue;
        makeRig (rig, index + 1000);
        rig->controller.selectVisualSlot (3);
        rig->drive ([&] { return rig->controller.snapshot().preloadedSongs.size() == 3; }, "benchmark preload finishes");
        preloaded.push_back (measure (*rig, 0, 1));
        rapid.push_back (measure (*rig, 1, 0, 2));
    }
    report ("first press", phase, firstPress);
    report ("revisit", phase, revisit);
    if (after) { report ("listed song", phase, preloaded); report ("rapid press", phase, rapid); }
}
}
