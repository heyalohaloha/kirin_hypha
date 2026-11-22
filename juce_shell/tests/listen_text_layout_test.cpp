#include "ValidationStorageSandbox.h"
#include "ListenTextLayoutTest.h"
#include "KeepTailNoticeLayoutTest.h"
#include "RecoveryGuidanceTest.h"
#include "LiveCompareFooterContractTest.h"
#include "LiveBlindUiContractTest.h"
#include "BlindTrialLayoutContractTest.h"
#include "VersionBlindScreenContractTest.h"
#include "BlindGuessImageTest.h"
#include "ListenAccessibleNamesTest.h"
#include "BlindImageNameEditingTest.h"
#include <stdexcept>

int main (int argc, char** argv)
try
{
    ValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI initialise;
    hypha::i18n::holdLanguage (true);
    if (argc == 2 && juce::String (argv[1]) == "--keep-tail-notice-only")
    {
        hypha::tests::verifyKeepTailNoticeLayout ([] (bool ok, const char* why)
            { if (!ok) throw std::runtime_error (why); });
        return 0;
    }
    const auto checkLayout = [] (bool ok, const char* why) { if (!ok) throw std::runtime_error (why); };
    if (argc == 2 && juce::String (argv[1]) == "--blind-trial-layout-only")
    {
        hypha::tests::verifyBlindTrialLayout (checkLayout);
        return 0;
    }
    hypha::tests::verifyBlindImageNameEditing();
    hypha::tests::verifyListenAccessibleNames();
    hypha::tests::verifyRecoveryGuidance ([] (bool ok, const char* why)
        { if (!ok) throw std::runtime_error (why); });
    if (argc == 2 && juce::String (argv[1]) == "--recovery-only") return 0;
    hypha::tests::verifyLiveCompareFooterContract();
    hypha::tests::verifyLiveBlindUiContract();
    hypha::tests::verifyBlindTrialLayout (checkLayout);
    hypha::tests::verifyVersionBlindScreen();
    hypha::tests::verifyBlindGuessAndImage ([] (bool ok, const char* why)
        { if (!ok) throw std::runtime_error (why); });
    hypha::tests::verifyFooterSurfaceRestoration ([] (bool ok, const char* why)
        { if (!ok) throw std::runtime_error (why); });
    hypha::tests::verifyAllListenText ([] (bool ok, const char* why)
        { if (!ok) throw std::runtime_error (why); });
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
