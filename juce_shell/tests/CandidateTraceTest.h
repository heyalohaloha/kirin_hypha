#pragma once
#include "LiveTimingFixtureAccess.h"

namespace hypha::tests
{
template<class Host, class Require> void verifyCandidateTrace (Host& host, Require require, const juce::File& storage)
{
    const auto root = storage.getChildFile ("candidate-trace-" + juce::String (host.block));
    require (root.createDirectory().wasOk(), "create a fixture-only trace directory");
    const auto saved = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_CHAIN_TRACE_DIR", {});
    const auto environment = [] (const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s ("KIRIN_HYPHA_CHAIN_TRACE_DIR", value.toRawUTF8());
       #else
        setenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR", value.toRawUTF8(), 1);
       #endif
    };
    environment (root.getFullPathName());
    auto recorder = chain_trace::Recorder::fromEnvironment();
    environment (saved);
    require (recorder != nullptr, "arm the actual trace recorder");
    LiveTimingFixtureAccess::traceForTest (host.post(), std::move (recorder));
    host.render (2, true, true);
    LiveTimingFixtureAccess::traceForTest (host.post(), {}); // joins and flushes; no timed polling
    const auto logs = root.findChildFiles (juce::File::findFiles, true, "blocks.jsonl");
    require (logs.size() == 1, "isolated POST callbacks write the real trace");
    int rows = 0;
    for (const auto& line : juce::StringArray::fromLines (logs[0].loadFileAsString()))
    {
        const auto row = juce::JSON::parse (line);
        if (! row.hasProperty ("role")) continue;
        require (static_cast<int> (row["role"]) == 1 && static_cast<bool> (row["active"])
            && static_cast<bool> (row["blockSeen"]) && static_cast<bool> (row["decision"]["seen"]),
            "trace records the real comparison input, output and renderer decision during isolation");
        ++rows;
    }
    require (rows == 2 && logs[0].getSiblingFile ("audio.f32").getSize() == host.channels * host.block * 2 * 2 * 4,
             "trace includes both sides of both actual callbacks");
}
}
