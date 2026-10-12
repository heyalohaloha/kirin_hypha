#include "ValidationStorageSandbox.h"
#include "ListenTextLayoutTest.h"
#include "LiveCompareFooterContractTest.h"
#include "LiveBlindUiContractTest.h"
#include "VersionBlindScreenContractTest.h"
#include <stdexcept>

int main()
try
{
    ValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI initialise;
    hypha::i18n::holdLanguage (true);
    hypha::tests::verifyLiveCompareFooterContract();
    hypha::tests::verifyLiveBlindUiContract();
    hypha::tests::verifyVersionBlindScreen();
    hypha::tests::verifyFooterSurfaceRestoration ([] (bool ok, const char* why)
        { if (!ok) throw std::runtime_error (why); });
    hypha::tests::verifyAllListenText ([] (bool ok, const char* why)
        { if (!ok) throw std::runtime_error (why); });
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
