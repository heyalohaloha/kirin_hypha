#pragma once
#include "DoublePrecisionParityTest.h"
#include "TestTemporaryDirectory.h"
#include "TestPathBudget.h"
#include <cstdlib>
namespace hypha::tests::precision
{
inline void traceEnvironment (const juce::String& value, bool present)
{
   #if JUCE_WINDOWS
    ::_putenv_s ("KIRIN_HYPHA_CHAIN_TRACE_DIR", present ? value.toRawUTF8() : "");
   #else
    if (present) ::setenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR", value.toRawUTF8(), 1);
    else ::unsetenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR");
   #endif
}
inline void verifyPrecisionTrace()
{
    const auto* old = std::getenv ("KIRIN_HYPHA_CHAIN_TRACE_DIR");
    struct Cleanup
    {
        juce::String previous; bool present; juce::File root;
        ~Cleanup() { traceEnvironment (previous, present); removeFixtureDirectory (root); }
    } cleanup {old != nullptr ? juce::String::fromUTF8 (old) : juce::String {}, old != nullptr,
               temporaryParent().getNonexistentChildFile ("precision-trace", {}, false)};
    check (cleanup.root.createDirectory().wasOk(), "trace fixture directory");
    for (int bits : {32, 64})
    {
        const auto root = cleanup.root.getChildFile (juce::String (bits));
        check (root.createDirectory().wasOk(), "precision-specific trace directory");
        traceEnvironment (root.getFullPathName(), true);
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            Processor processor (Processor::Role::Post); Host host;
            configure (processor, 2, host, bits == 64);
            juce::MidiBuffer midi;
            if (bits == 64)
            {
                juce::AudioBuffer<double> audio (2, 128);
                for (int c = 0; c < 2; ++c) for (int f = 0; f < 128; ++f) audio.setSample (c, f, std::nextafter (0.125, 1.0));
                beginReferenceRtProbe(); processor.processBlock (audio, midi);
                check (endReferenceRtProbe() == 0, "double trace callback allocates no C++ heap");
            }
            else
            {
                juce::AudioBuffer<float> audio (2, 128);
                for (int c = 0; c < 2; ++c) for (int f = 0; f < 128; ++f) audio.setSample (c, f, 0.125f);
                processor.processBlock (audio, midi);
            }
            processor.releaseResources();
        } // The real IO writer joins and flushes before its output is read.
        bool found = false;
        for (const auto& file : root.findChildFiles (juce::File::findFiles, true, "blocks.jsonl"))
        {
            juce::StringArray lines; lines.addLines (file.loadFileAsString());
            for (const auto& line : lines)
            {
                const auto json = juce::JSON::parse (line);
                if (json["type"] != "block") continue;
                check (static_cast<int> (json["precisionBits"]) == bits && static_cast<int> (json["frames"]) == 128,
                       "the actual callback records the host precision in trace JSON");
                found = true;
            }
        }
        check (found, "the trace contains a real processed block");
    }
    std::cout << "precision trace: actual 32/64 bit callbacks, JSON readback and double RT heap PASS\n";
}
}
