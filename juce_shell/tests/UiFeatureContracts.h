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

namespace hypha::tests
{
inline bool verifyUiFeatureContracts (int argc, char** argv)
{
    const bool entryOnly = argc == 2 && std::string_view (argv[1]) == "--product-entry-only";
    if (argc != 1 && ! entryOnly)
    {
        std::cerr << "Usage: KirinUiRenderContractTests [--product-entry-only]\n";
        std::exit (EXIT_FAILURE);
    }
    verifyInformationContract();
    verifyTimePageNavigationContract();
    if (entryOnly)
    {
        std::cout << "Product entry: PASS (82 role/size layouts, update dispatch, DRUM navigation)\n";
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
    verifyObservatoryViewContract();
    verifyCaptureHistoryContract();
    verifyTimeHistoryContract();
    verifySpaceFieldContract();
    verifyReferenceAuditionComponentContract(); verifyOsAccessUiContract();
    return false;
}
}
