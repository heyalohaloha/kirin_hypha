// ABCV（A／B／C／V）の Hypha 側の契約テストをまとめて呼ぶ。入口の main の行数を増やさないため。
#include <juce_core/juce_core.h>
#include "TestPathBudget.h"
void testReferenceLibrarySets (const juce::File&);
void testReferenceLibrarySkipped (const juce::File&);
void testReferenceLiveWindow();
void testReferenceTrackingRules();
void testReferenceCueMatch (const juce::File&);
void testReferenceMonoVersion (const juce::File&, const juce::String&);
void testReferenceRoles (const juce::File&);
void testReferenceCueRestart (const juce::File&);
void testReferenceLowerA (const juce::File&);
void testReferenceKirinSpectrum();
void testReferenceBlauertBands();
void testReferenceDynamicsRange();
void testReferenceKirinFingerprint();
void testReferenceAutoVersion (const juce::File&);
void testReferenceAbcv (const juce::File&);
bool runReferenceAbcvTests (int argc, char** argv, const juce::File&);

void testReferenceAbcv (const juce::File& sandbox)
{
    testReferenceLibrarySets (sandbox);
    testReferenceLibrarySkipped (sandbox);  // 2026-10-06：受け付けない 1 項目だけを外す
    testReferenceLiveWindow();
    testReferenceTrackingRules();
    testReferenceCueMatch (sandbox);
    testReferenceMonoVersion (sandbox, "follow");
    testReferenceRoles (sandbox);
    testReferenceCueRestart (sandbox);
    testReferenceLowerA (sandbox);       // 2026-10-03：上限超えは承認して A を下げて合わせる（R-12）
    testReferenceKirinSpectrum();
    testReferenceBlauertBands();         // 2026-10-04：Blauert の帯の差
    testReferenceDynamicsRange();        // 2026-10-04：範囲の帯（Kirin OS と同じ区間の値）
    testReferenceKirinFingerprint();
    testReferenceAutoVersion (sandbox);  // 2026-10-06：V の AUTO のしきい値と選び直しの表
}

// `--abcv-only`：ABCV のテストだけを流す（手元で直すときに全体の 4 分を待たない）。
bool runReferenceAbcvTests (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2) return false;
    const juce::String mode (argv[1]);
    if (mode == "--library-skipped-only") testReferenceLibrarySkipped (sandbox);
    else if (mode == "--abcv-only") testReferenceAbcv (sandbox);
    else return false;
    hypha::tests::removeFixtureDirectory (sandbox);
    return true;
}
