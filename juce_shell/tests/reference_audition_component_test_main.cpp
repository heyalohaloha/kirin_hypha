#include "ReferenceAuditionComponentContractTest.h"

#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    hypha::tests::verifyReferenceAuditionComponentContract();
    std::cout << "Reference audition component contract tests passed\n";
    return 0;
}
