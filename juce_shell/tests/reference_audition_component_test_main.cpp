#include "ReferenceAuditionComponentContractTest.h"
#include "ReferenceMissingCheckTest.h"
#include "ReferenceTextRepairsTest.h"
#include "ValidationStorageSandbox.h"

#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    ValidationStorageSandbox storage;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc == 3 && juce::String (argv[1]) == "--reference-text-snapshots")
    { hypha::tests::reference_text_repairs::exportSnapshots (juce::File (argv[2])); return 0; }
    const juce::String mode (argc == 2 ? argv[1] : "");
    if (mode == "--missing-check-navigation-only") { hypha::tests::verifyMissingCheckNavigation(); return 0; }
    if (mode == "--missing-check-text-only") { hypha::tests::verifyMissingCheckText(); return 0; }
    namespace repairs = hypha::tests::reference_text_repairs;
    if (mode == "--reference-approval-only") { repairs::verifyApprovalPreview(); return 0; }
    if (mode == "--reference-c-legend-only") { repairs::verifyLegends (true); return 0; }
    if (mode == "--reference-b-legend-only") { repairs::verifyLegends (false); return 0; }
    if (mode == "--reference-c-match-fit-only") { repairs::verifyCMatchFit(); return 0; }
    if (mode == "--reference-onset-only") { repairs::verifyOnsetMeaning(); return 0; }
    if (mode == "--reference-cue-label-only") { repairs::verifyCueLabels(); return 0; }
    hypha::tests::verifyMissingCheckNavigation();
    hypha::tests::verifyMissingCheckText();
    repairs::verifyApprovalPreview(); repairs::verifyLegends (true); repairs::verifyLegends (false);
    repairs::verifyCMatchFit(); repairs::verifyOnsetMeaning(); repairs::verifyCueLabels();
    hypha::tests::verifyReferenceAuditionComponentContract();
    std::cout << "Reference audition component contract tests passed\n";
    return 0;
}
