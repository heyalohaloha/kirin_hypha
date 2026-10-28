#pragma once
#include "ReferenceVersionPositionTest.h"
#include "ReferenceVersionLoopTest.h"
#include "ReferenceVersionSwitchTest.h"
#include "ReferencePageStartDiagnostic.h"
#include "ReferenceWorkingMixTest.h"
#include "ReferenceShiftedMixAlignmentTest.h"
#include "ReferenceCheckSwitchTest.h"
void testReferenceLibrarySets (const juce::File&);
#include "TestPathBudget.h"
inline bool runReferenceRepairTests (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2) return false;
    const juce::String mode (argv[1]);
    if (mode == "--position-repairs-only") testReferenceVersionPosition (sandbox);
    else if (mode == "--version-loop-only") testReferenceVersionLoop (sandbox);
    else if (mode == "--version-splice-proof-only") testReferenceVersionLoop (sandbox, false, false, true);
    else if (mode == "--version-fresh-proof-only") testReferenceVersionLoop (sandbox, false, true);
    else if (mode == "--version-mix-loop-only") testReferenceVersionLoop (sandbox, true);
    else if (mode == "--page-seek-publication-only")
    { for (bool switching : { false, true }) testReferencePageStart (sandbox, switching, false, false, false, PageWaitChange::seekPublication); }
    else if (mode == "--page-range-only")
    { for (bool switching : { false, true }) for (auto change : { PageWaitChange::negative, PageWaitChange::atEnd, PageWaitChange::pastEnd }) testReferencePageStart (sandbox, switching, false, false, false, change); }
    else if (mode == "--page-read-wait-only")
    { for (bool switching : { false, true }) for (auto change : { PageWaitChange::slowPages, PageWaitChange::readFailure, PageWaitChange::sourceGone }) testReferencePageStart (sandbox, switching, false, false, false, change); }
    else if (mode == "--page-switch-only") { testReferencePageStart (sandbox, true); testReferencePageStart (sandbox, true, false, true); testReferencePageStart (sandbox, true, false, false, true); testReferencePageStart (sandbox, true, false, false, false, PageWaitChange::transportStop); testReferencePageStart (sandbox, true, false, false, false, PageWaitChange::newSelection); }
    else if (mode == "--page-first-only") { testReferencePageStart (sandbox, false); testReferencePageStart (sandbox, false, false, true); testReferencePageStart (sandbox, false, false, false, true); testReferencePageStart (sandbox, false, false, false, false, PageWaitChange::transportStop); testReferencePageStart (sandbox, false, false, false, false, PageWaitChange::newSelection); }
    else if (mode == "--version-switch-only") testReferenceVersionSwitch (sandbox);
    else if (mode == "--version-edited-switch-only") testReferenceVersionSwitch (sandbox, true);
    else if (mode == "--content-copy-proof-only") testReferenceWorkingMix (sandbox, true, true);
    else if (mode == "--content-proof-only") testReferenceWorkingMix (sandbox, true);
    else if (mode == "--working-mix-only") testReferenceWorkingMix (sandbox);
    else if (mode == "--shifted-mix-only") testReferenceShiftedMixAlignment (sandbox);
    else if (mode == "--shifted-mix-48-only") testReferenceShiftedMixAlignment (sandbox, 48000);
    else if (mode == "--shifted-mix-192-only") testReferenceShiftedMixAlignment (sandbox, 192000);
    else if (mode == "--check-switch-only") testReferenceCheckSwitch (sandbox);
    else if (mode == "--calibration-repairs-only") testReferenceCalibrationRegressions (sandbox);
    else if (mode == "--comparison-repair-only") testReferenceComparisons (sandbox);
    else if (mode == "--content-alignment-only") testReferenceContentAlignment (sandbox);
    else if (mode == "--source-repairs-only")
    {
        testReferenceVersionPosition (sandbox);
        testReferenceVersionLoop (sandbox);
        testReferenceVersionLoop (sandbox, true);
        testReferenceVersionLoop (sandbox, false, true);
        testReferenceVersionLoop (sandbox, false, false, true);
        testReferenceVersionSwitch (sandbox);
        testReferenceVersionSwitch (sandbox, true);
        testReferenceWorkingMix (sandbox);
        testReferenceWorkingMix (sandbox, true);
        testReferenceWorkingMix (sandbox, true, true);
        testReferenceShiftedMixAlignment (sandbox);
        testReferenceCheckSwitch (sandbox);
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
