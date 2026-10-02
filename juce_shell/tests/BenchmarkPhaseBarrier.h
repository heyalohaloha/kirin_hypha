#pragma once
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <iostream>

namespace hypha::test
{
// Optional paired-process fixture setup. Only the message thread touches these files, before
// the 128-block warmup, never processBlock or the timed intervals. Ordinary CTests do no I/O
// here. A fresh external directory is mandatory; old measurements/markers are not overwritten.
class BenchmarkPhaseBarrier
{
public:
    BenchmarkPhaseBarrier()
    {
        const auto* path = std::getenv ("KIRIN_BENCHMARK_PHASE_DIR");
        const auto* identity = std::getenv ("KIRIN_BENCHMARK_PHASE_ROLE");
        if (path == nullptr && identity == nullptr) return;
        check (path != nullptr && identity != nullptr, "both paired fixture inputs are required");
        role = identity;
        check (role == "baseline" || role == "candidate", "paired fixture role is exact");
        check (juce::File::isAbsolutePath (path), "paired fixture directory must be absolute");
        directory = juce::File (path);
        check (directory.isDirectory(), "paired fixture directory must already exist");
        enabled = true;
    }

    bool ready (int phase)
    {
        if (! enabled) return true;
        check (phase >= 0 && phase < 3, "paired fixture phase is valid");
        const auto marker = directory.getChildFile (juce::String (phase) + "-" + role + ".ready");
        if (! announced[phase])
        {
            check (! marker.exists(), "paired fixture marker is fresh");
            check (marker.create().wasOk(), "paired fixture marker is created");
            announced[phase] = true;
        }
        const auto peer = role == "baseline" ? "candidate" : "baseline";
        return directory.getChildFile (juce::String (phase) + "-" + peer + ".ready").existsAsFile();
    }

private:
    static void check (bool ok, const char* message)
    {
        if (! ok) { std::cerr << "Benchmark phase barrier: " << message << '\n'; std::exit (EXIT_FAILURE); }
    }
    juce::File directory;
    juce::String role;
    bool enabled = false, announced[3] {};
};
}
