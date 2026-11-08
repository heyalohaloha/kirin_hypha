#include "ReferenceValidationStorageSandbox.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <stdexcept>

#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif
namespace hypha::tests
{
void verifyShortcutEditorActions();
void verifyShortcutTable();
void verifyShortcutKeys();
void verifyShortcutMarks();
#if ! KIRIN_HYPHA_PRE_DISPLAY
void verifyShortcutBlindScreenActions();
void verifyShortcutReferenceMoves();
#endif
}

int main (int argc, char** argv)
try
{
    if (argc != 2 || juce::String (argv[1]) != "actions")
        throw std::runtime_error ("Unknown shortcut test case");
    ReferenceValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI initialise;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    hypha::tests::verifyShortcutTable();
    hypha::tests::verifyShortcutEditorActions();
    hypha::tests::verifyShortcutKeys();
    hypha::tests::verifyShortcutMarks();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    hypha::tests::verifyShortcutBlindScreenActions();
    hypha::tests::verifyShortcutReferenceMoves();
   #endif
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
