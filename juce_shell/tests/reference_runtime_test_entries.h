#pragma once
#include <juce_core/juce_core.h>

void testRuntimeV2Workspace (const juce::File& sandbox);
void testRuntimeV2SourceCache();
void testReferenceLibraryContract (const juce::File&);
void testReferenceAbcv (const juce::File&);
bool runReferenceAbcvTests (int argc, char** argv, const juce::File&);
void testReferenceComparisons (const juce::File&);
void testReferencePendingAudition (const juce::File&);
bool runReferencePendingTests (int argc, char** argv, const juce::File&);
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

inline bool finishReferenceAbcMode (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2 || juce::String (argv[1]) != "--abc-only") return false;
    finishReferenceRegressionFixture (sandbox);
    return true;
}

void testReferenceVisual (const juce::File&);
void testReferenceAInput();  // A の受け渡し・Blind の枠・解析のフレーム数の上限（reference_a_input_test.cpp）
