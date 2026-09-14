#pragma once
#include <juce_core/juce_core.h>
void testReferenceCaptureMemory();
void testReferenceCaptureMemoryCase(int,int);
inline bool runReferenceCaptureMemory(int argc,char** argv)
{
    if(argc==4 && juce::String(argv[1])=="--capture-memory-case") {
        testReferenceCaptureMemoryCase(juce::String(argv[2]).getIntValue(),juce::String(argv[3]).getIntValue()); return true;
    }
    if(argc!=2 || juce::String(argv[1])!="--capture-memory-only") return false;
    testReferenceCaptureMemory(); return true;
}

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

void testReferenceACapture(const juce::File&);

void testReferenceCaptureEvidence(const juce::File&);
void testCaptureStartRace();
void testCaptureLiveSharing();
inline bool runReferenceCaptureTests(int argc,char** argv,const juce::File& sandbox)
{
    const auto mode=argc==2 ? juce::String(argv[1]) : juce::String();
    if(mode=="--capture-repair-only") { testCaptureStartRace(); testCaptureLiveSharing(); require(sandbox.deleteRecursively(),"repair cleanup"); return true; }
    if(mode!="--capture-evidence-only") testReferenceACapture(sandbox);
    if(mode!="--capture-only") testReferenceCaptureEvidence(sandbox);
    if(mode!="--capture-only" && mode!="--capture-evidence-only") { testCaptureStartRace(); testCaptureLiveSharing(); return false; }
    require(sandbox.deleteRecursively(),"capture fixture cleanup"); return true;
}
