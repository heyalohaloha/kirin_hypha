#pragma once
void testReferenceLibrarySets (const juce::File&);
#include "TestPathBudget.h"
inline bool runReferenceRepairTests (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2) return false;
    const juce::String mode (argv[1]);
    if (mode == "--source-repairs-only")
    {
        testReferenceLibraryContract (sandbox);
        testReferenceLibrarySets (sandbox);
        require (writeStereoWav (sandbox.getChildFile ("version-a.wav")), "source repair fixture audio");
        testRuntimeV2Workspace (sandbox);
    }
    else if (mode == "--reference-format-only") testReferenceLibraryFormat (sandbox);
    else if (mode == "--event-repairs-only") testRuntimeEventTransport (sandbox);
    else return false;
    require (hypha::tests::removeFixtureDirectory (sandbox), "remove only the Reference repair fixture");
    return true;
}
