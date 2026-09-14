#pragma once
#include "local_blind/LocalBlindAdmission.h"

namespace hypha::local_blind_ui
{
inline const char* admissionText (local_blind::CaptureAdmission reason) noexcept
{
    using A = local_blind::CaptureAdmission;
    switch (reason)
    {
        case A::ready: return "DAW: play the section. HYPHA: capture 4 seconds.";
        case A::unsupported: return "This format cannot start PRE / POST Blind.";
        case A::recovery: return "Finish the current comparison and return to Live.";
        case A::releasePending: return "The previous capture is still releasing its resources.";
        case A::pairRequired: return "Choose the PRE to compare with this POST.";
        case A::keepBusy: return "Finish the current Keep / Record before capturing.";
        case A::referenceBusy: return "Finish Reference Blind and return to Live first.";
        case A::captureBusy: return "The previous audio capture is still in use.";
        case A::playbackRequired: return "DAW: start playback, then capture the section.";
        case A::clockUnavailable: return "DAW sample position or format is not available for capture.";
        case A::engineUnavailable: return "The measurement engine is not available.";
        case A::admissionFailed: return "Capture could not be admitted. Check the current pair and comparisons.";
        case A::requestFailed: return "The capture request could not be sent. Try again after it is released.";
    }
    return "Capture could not start.";
}
}
