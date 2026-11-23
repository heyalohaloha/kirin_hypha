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
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace
{
std::size_t unsignedArg (std::string_view text)
{
    std::size_t value {};
    const auto result = std::from_chars (text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size())
        throw std::runtime_error ("expected a numeric layout shard index and count");
    return value;
}
}

int main (int argc, char** argv)
try
{
    using namespace hypha::tests;
    const std::string_view mode = argc >= 2 ? argv[1] : "";
    const bool all = argc == 1;
    const bool layout = mode == "--blind-trial-layout-only" || mode == "--paint-only";
    ListenLayoutShard shard;
    if (argc == 4 && layout)
    {
        shard = { unsignedArg (argv[2]), unsignedArg (argv[3]) };
        const std::size_t maximum = mode == "--paint-only" ? 12 : 17;
        if (shard.count == 0 || shard.count > maximum || shard.index >= shard.count)
            throw std::runtime_error ("layout shard out of range");
    }
    else if (argc != 1 && argc != 2)
        throw std::runtime_error ("layout test expects a mode or a mode, index and count");

    ValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI initialise;
    hypha::i18n::holdLanguage (true);
    const auto require = [] (bool ok, const char* why) { if (!ok) throw std::runtime_error (why); };
    if (mode == "--keep-tail-notice-only") verifyKeepTailNoticeLayout (require);
    else if (all || mode == "--recovery-only")
    {
        verifyBlindImageNameEditing();
        verifyListenAccessibleNames();
        verifyRecoveryGuidance (require);
    }
    if (all || mode == "--footer-only") verifyLiveCompareFooterContract();
    if (all || mode == "--blind-ui-only") verifyLiveBlindUiContract();
    if (all || mode == "--blind-trial-layout-only") verifyBlindTrialLayout (require, shard);
    if (all || mode == "--blind-ui-only") verifyVersionBlindScreen();
    if (all || mode == "--blind-image-only") verifyBlindGuessAndImage (require);
    if (all || mode == "--footer-restoration-only") verifyFooterSurfaceRestoration (require);
    if (all || mode == "--paint-only") verifyAllListenText (require, shard);
    if (!all && !layout && mode != "--keep-tail-notice-only" && mode != "--recovery-only"
        && mode != "--footer-only" && mode != "--blind-ui-only" && mode != "--blind-image-only"
        && mode != "--footer-restoration-only")
        throw std::runtime_error ("unknown layout test mode");
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
