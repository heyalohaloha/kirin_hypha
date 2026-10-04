// ABCV（A／B／C／V）の Hypha 側の契約テストをまとめて呼ぶ。入口の main の行数を増やさないため。
#include <juce_core/juce_core.h>

void testReferenceLibrarySets (const juce::File&);
void testReferenceLiveWindow();
void testReferenceTrackingRules();
void testReferenceCueMatch (const juce::File&);
void testReferenceRoles (const juce::File&);
void testReferenceCueRestart (const juce::File&);
void testReferenceLowerA (const juce::File&);
void testReferenceKirinSpectrum();
void testReferenceBlauertBands();
void testReferenceDynamicsRange();
void testReferenceKirinFingerprint();
void testReferenceAbcv (const juce::File&);
bool runReferenceAbcvTests (int argc, char** argv, const juce::File&);

void testReferenceAbcv (const juce::File& sandbox)
{
    testReferenceLibrarySets (sandbox);
    testReferenceLiveWindow();
    testReferenceTrackingRules();
    testReferenceCueMatch (sandbox);
    testReferenceRoles (sandbox);
    testReferenceCueRestart (sandbox);
    testReferenceLowerA (sandbox);       // 2026-10-03：上限超えは承認して A を下げて合わせる（R-12）
    testReferenceKirinSpectrum();
    testReferenceBlauertBands();         // 2026-10-04：Blauert の帯の差
    testReferenceDynamicsRange();        // 2026-10-04：範囲の帯（Kirin OS と同じ区間の値）
    testReferenceKirinFingerprint();
}

// `--abcv-only`：ABCV のテストだけを流す（手元で直すときに全体の 4 分を待たない）。
bool runReferenceAbcvTests (int argc, char** argv, const juce::File& sandbox)
{
    if (argc != 2 || juce::String (argv[1]) != "--abcv-only") return false;
    testReferenceAbcv (sandbox);
    sandbox.deleteRecursively();
    return true;
}
