#include "ChainClockPolicyContract.h"
#include <stdexcept>
#include "ValidationStorageSandbox.h"
#include "../src/HyphaLanguage.h"
#include "LanguageContract.h"
#include "LiveTimingAvailabilityUiTest.h"
#include "FooterNoticeContractChecks.h"
#include "AuCycleCallbackGuidanceTest.h"
#include "AuCycleGuidanceUiTest.h"
#include "AuCycleSpectrumUiTest.h"
#include "AuCycleSpectrumCallbackTest.h"
#include "ReaperTimingUiTest.h"
#include "ReaperListenFeedbackTest.h"

int main (int argc, char** argv)
try
{
    ValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI initialise;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    hypha::i18n::holdLanguage (true);
    if (argc == 2 && juce::String (argv[1]) == "--listen-feedback-only")
    {
        hypha::tests::editor_product::verifyReaperListenFeedback();
        return 0;
    }
    if (argc == 2 && juce::String (argv[1]) == "--reaper-ui-only")
    {
        hypha::tests::verifyReaperTimingUi ([] (bool ok, const char* why) { if (!ok) throw std::runtime_error (why); });
        return 0;
    }
    hypha::tests::verifyAuCycleCallbackGuidance();
    hypha::tests::editor_product::verifyReaperListenFeedback();
    hypha::tests::verifyAuCycleSpectrumUi();
    hypha::tests::verifyAuCycleSpectrumCallbacks();
    hypha::tests::verifyAuCycleGuidanceUi();
    hypha::tests::verifyChainClockPolicy();
    hypha::tests::language_contract::verifyLookup();
    hypha::tests::verifyAbsoluteAnalysisHelp();
    hypha::tests::verifyLiveTimingAvailabilityUi ([] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); });
    hypha::tests::verifyReaperTimingUi ([] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); });
    hypha::tests::verifyOtherHostListenText ([] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); });
    using namespace hypha::live_compare_clock_policy;
    using Authority = hypha::live_compare::ClockAuthority;
    using Wrapper = juce::AudioProcessor;
    const auto require = [] (bool value) { if (! value) throw std::runtime_error ("host content policy"); };
    int policyCases = 0, policyFailures = 0;
    const auto expect = [&] (Wrapper::WrapperType wrapper, const char* name, const char* version,
                            Authority authority, bool measured = false)
    {
        ++policyCases;
        const auto c = classify (wrapper, name, version, 96000);
        if (c.authority == authority && c.measured == measured) return;
        ++policyFailures;
        std::cerr << "Host policy mismatch: " << name << " [" << version << "] wrapper="
                  << static_cast<int> (wrapper) << '\n';
    };
    for (const auto* version : { "6.0.0", "6.0.0.0", "6.0.0 Build 0", "6.6.3.104431",
                                 "7.2.3 Build 108761", "7.2.3.108761" })
    {
        expect (Wrapper::wrapperType_VST3, "Studio One", version, Authority::certifiedContent);
    }
    for (const auto* version : { "8.0.0", "8.0.2.111888", "8.1.2.0", "8.1.2.113408", "8.99.99 Build 123456" })
    {
        expect (Wrapper::wrapperType_VST3, "Studio Pro", version, Authority::certifiedContent);
    }
    for (const auto* version : { "8.1.2 Build 113407", "8.1.2.113407" })
        expect (Wrapper::wrapperType_VST3, "Studio Pro", version, Authority::certifiedContent, true);
    for (const auto* name : { "Studio One", "Studio Pro" })
        for (const auto* version : { "", "unknown", "5.4.9", "5.5.0", "5.5.2 Build 86528", "5.9.99",
                                     "4.9.0", "6.0", "6.0.x", "8..1", "8.1.x", "8.1.2dev",
                                     "8.1.2 Build ", "8.1.2 Build -1", "8.1.2.3 Build 4", "999999999999.1.1", "9.0.0" })
            expect (Wrapper::wrapperType_VST3, name, version, Authority::none);
    require (! studioBuildCertified ("Studio One", "8.0.0"));
    require (! studioBuildCertified ("Studio Pro", "7.2.3"));
    require (! studioBuildCertified ("Studio One 7", "7.2.3"));
    for (const auto* version : { "5.5.2", "6.0.0" })
        expect (Wrapper::wrapperType_VST3, "studioapp", version, Authority::none);
    for (const auto* name : { "REAPER", "reaper" })
    {
        for (const auto* version : { "0.999", "1", "1.0", "6.83", "7.70", "7.71", "7.71.0_ba637e1u",
                                     "7.72", "7.71+dev", "7.71.0", "7.71.0_unknown", "9.0" })
            expect (Wrapper::wrapperType_VST3, name, version, Authority::certifiedProject);
        for (const auto* version : { "", "unknown", "v7.71", " 7.71", "\t7.71", ".7.71", "+7.71", "-7.71" })
            expect (Wrapper::wrapperType_VST3, name, version, Authority::none);
    }
    for (const auto* name : { "", "Reaper", "REAPER Helper", "REAPER.app", "Other Host" })
        for (const auto* version : { "7.71", "7.72" })
            expect (Wrapper::wrapperType_VST3, name, version, Authority::none);
    for (const auto wrapper : { Wrapper::wrapperType_AudioUnit, Wrapper::wrapperType_AAX,
                                Wrapper::wrapperType_VST, Wrapper::wrapperType_Standalone })
    {
        for (const auto* name : { "REAPER", "reaper" })
            for (const auto* version : { "7.71", "7.72" })
                expect (wrapper, name, version, Authority::none);
        expect (wrapper, "Studio One", "6.0.0", Authority::none);
        expect (wrapper, "Studio One", "7.2.3", Authority::none);
        expect (wrapper, "Studio Pro", "8.1.2.113407", Authority::none);
    }
    expect (Wrapper::wrapperType_AudioUnit, "Logic Pro", "12.2", Authority::logicRender, true);
    expect (Wrapper::wrapperType_AudioUnit, "Logic Pro", "12.3", Authority::logicRender);
    expect (Wrapper::wrapperType_AudioUnit, "Logic Pro", "unknown", Authority::none);
    expect (Wrapper::wrapperType_AudioUnit, "Other AU", "12.2", Authority::none);
    expect (Wrapper::wrapperType_VST3, "Logic Pro", "12.2", Authority::none);
    expect (Wrapper::wrapperType_AAX, "Logic Pro", "12.2", Authority::none);
    std::cout << "Host version policy: " << policyCases << " cases, " << policyFailures << " failures\n";
    require (policyFailures == 0);
    for (auto language : { hypha::i18n::Language::english, hypha::i18n::Language::japanese })
    {
        const hypha::i18n::ScopedLanguage scoped (language);
        for (int width : { 300, 375, 450 })
        {
            hypha::observatory::View view (hypha::observatory::Role::post); view.setVisible (true);
            view.setSize (width, width * 2 / 3);
            hypha::live_compare::Status status;
            status.active = status.clockUnavailable = status.preSelected = status.preWaiting = true;
            status.observation = hypha::live_compare::RecoveryReason::clockMissing;
            const auto* notice = hypha::live_compare_ui::namedRecovery (status);
            view.setFeedback (notice);
            require (hypha::tests::footer_notice::retainedInFooter (view, notice));
        }
    }
    std::cout << "Host content clock: version floors, native spellings, measured and wrapper boundaries PASS\n";
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
