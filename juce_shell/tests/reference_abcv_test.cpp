// ABCV（A／B／C／V）の Hypha 側の契約テストをまとめて呼ぶ。入口の main の行数を増やさないため。
#include <juce_core/juce_core.h>

void testReferenceLibrarySets (const juce::File&);
void testReferenceAbcv (const juce::File&);

void testReferenceAbcv (const juce::File& sandbox)
{
    testReferenceLibrarySets (sandbox);  // H1
}
