#pragma once

#include "ObservationPageContractTest.h"
#include "ObservatoryCompositeContractTest.h"
#include "PerceptualHistoryContractTest.h"
#include "TimePageNavigationContractTest.h"
#include "SpectrumFocusTrailContractTest.h"
#include "SpectrumPresentationContractTest.h"
#include "RunSummaryContractTest.h"
#include "GuideFrequencyOverlayContractTest.h"
#include "AbsoluteTimelineContractTest.h"
#include "AbsoluteSpectrumContractTest.h"
#include "ObservatoryViewContractTest.h"
#include "CaptureHistoryContractTest.h"
#include "TimeHistoryContractTest.h"
#include "SpaceFieldContractTest.h"
#include "ReferenceAuditionComponentContractTest.h"
#include "OsAccessUiContractTest.h"
#include "HyphaInformationContractTest.h"
#include "HybridVuContractTest.h"
#include "LocalBlindUiContractTest.h"
#include "ReferenceAccessPanelContractTest.h"
#include "ObservationEqualityContractTest.h"
#include "PolylineGeometryContractTest.h"
#include "SpectrumPerformanceFixture.h"
#include "SpectrumResponsiveGeometryContractTest.h"
#include "TypographyContractTest.h"

namespace hypha::tests
{
inline bool verifyUiFeatureContracts (int argc, char** argv)
{
    const bool entryOnly = argc == 2 && std::string_view (argv[1]) == "--product-entry-only";
    const bool updatesOnly = argc == 2 && std::string_view (argv[1]) == "--observation-update-only";
    const bool focusOnly = argc == 2 && std::string_view (argv[1]) == "--spectrum-focus-only";
    const bool hybridVuOnly = argc == 2 && std::string_view (argv[1]) == "--hybrid-vu-only";
    const bool typographyOnly = argc == 2 && std::string_view (argv[1]) == "--typography-only";
    const bool typographyVisualOnly = argc == 2
        && std::string_view (argv[1]) == "--typography-visual-only";
    if (argc != 1 && ! entryOnly && ! updatesOnly && ! focusOnly && ! hybridVuOnly
        && ! typographyOnly && ! typographyVisualOnly)
    {
        std::cerr << "Usage: KirinUiRenderContractTests [--product-entry-only|"
                     "--observation-update-only|--spectrum-focus-only|--hybrid-vu-only|"
                     "--typography-only|--typography-visual-only]\n";
        std::exit (EXIT_FAILURE);
    }
    observation_equality_contract::verify();
    verifyPolylineGeometryContract();
    verifySpectrumResponsiveGeometry();
    if (updatesOnly) return true;
    verifyTypographyContract();
    if (typographyOnly) return true;
    if (typographyVisualOnly)
    {
        verifyObservatoryViewContract();
        verifyObservatoryCompositeContract();
        return true;
    }
    if (hybridVuOnly)
    {
        verifyHybridVuContract();
        return true;
    }
    if (focusOnly)
    {
        std::cout << std::unitbuf << "Starting focused Spectrum contracts\n";
        verifySpectrumFocusTrailContract();
        verifySpectrumFocusTrailRendering (spectrumPerformanceFixture());
        return true;
    }
    verifyInformationContract();
    verifyReferenceAccessPanelContract();
    verifyReferenceAuditionComponentContract();
    verifyOsAccessUiContract();
    verifyTimePageNavigationContract();
    verifyLocalBlindUiContract();
    if (entryOnly)
    {
        std::cout << "Product entry: PASS (82 role/size layouts, 41 Reference layouts, update dispatch, DRUM navigation)\n";
        return true;
    }
    verifyObservationPageContract();
    verifyObservatoryCompositeContract();
    verifyPerceptualHistoryContract();
    verifySpectrumFocusTrailContract();
    verifySpectrumPresentationContract();
    verifyRunSummaryContract();
    verifyGuideFrequencyOverlayContract();
    verifyAbsoluteTimelineContract();
    verifyAbsoluteSpectrumContract();
    verifyPerceptualRenderingContract();
    verifyHybridVuContract();
    verifyObservatoryViewContract();
    verifyCaptureHistoryContract();
    verifyTimeHistoryContract();
    verifySpaceFieldContract();
    return false;
}
}
