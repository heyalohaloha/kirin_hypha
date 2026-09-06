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

namespace hypha::tests
{
inline void verifyUiFeatureContracts()
{
    verifyObservationPageContract();
    verifyObservatoryCompositeContract();
    verifyPerceptualHistoryContract();
    verifyTimePageNavigationContract();
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
}
}
