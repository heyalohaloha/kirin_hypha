#pragma once
#include <juce_core/juce_core.h>

void testRuntimeV2Workspace (const juce::File& sandbox);
void testRuntimeV2SourceCache();
void testReferenceLibraryContract (const juce::File&);
void testReferenceComparisons (const juce::File&);
bool testReferenceLibraryOsFixture();
