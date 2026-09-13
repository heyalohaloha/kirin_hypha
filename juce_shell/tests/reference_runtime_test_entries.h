#pragma once
#include <juce_core/juce_core.h>

void testRuntimeV2Workspace (const juce::File& sandbox);
void testRuntimeV2SourceCache();
void testReferenceLibraryContract (const juce::File&);
void testReferenceComparisons (const juce::File&);
bool testReferenceLibraryOsFixture();

void testReferenceContentAlignment (const juce::File&);
void testReferenceRealContentAlignment();

void testReferenceCalibrationRegressions (const juce::File&);

inline void finishReferenceRegressionFixture (const juce::File& sandbox)
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_KEEP_FIXTURE", {}).isNotEmpty())
        std::cout << "Reference regression fixture: " << sandbox.getFullPathName() << '\n';
    else require (sandbox.deleteRecursively(), "ABC fixtures must be removed");
}

void testReferenceVisual (const juce::File&);
