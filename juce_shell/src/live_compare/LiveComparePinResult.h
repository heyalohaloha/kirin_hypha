#pragma once

#include "../local_blind/LocalBlindAdmission.h"
#include "LiveComparePin.h"

namespace hypha::live_compare
{
// What PIN did: fixed the window and handed it to PRE / POST Blind, or why not. A pin that failed
// never touches Blind; a Blind admission that failed releases what it took.
struct LivePinResult
{
    bool pinned = false;
    PinFailure pin = PinFailure::none;
    local_blind::CaptureAdmission admission = local_blind::CaptureAdmission::ready;
};
}
