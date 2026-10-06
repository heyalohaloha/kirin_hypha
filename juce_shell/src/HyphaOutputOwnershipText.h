#pragma once

#include "OutputOwnership.h"

namespace hypha::output_owner
{
// 表が断った理由を、押した利用者に言う文（R-28。日本語は表示のときに訳す）。
inline const char* refusalText (Reason reason) noexcept
{
    switch (reason)
    {
        case Reason::layout:             return "Mono / stereo only";
        case Reason::restoring:          return "LISTEN is restoring; try again in a moment";
        case Reason::liveReturning:      return "Ended; normal level returns with audio";
        case Reason::returnFirst:        return "Press RETURN first";
        case Reason::liveComparison:     return "End the current comparison first";
        case Reason::blindRunning:       return "End Blind Compare first";
        case Reason::recordRunning:      return "Finish Keep / Record first";
        case Reason::referenceReturning: return "A is returning to normal level";
        case Reason::auditionRunning:    return "Press A in REF first";
        case Reason::none:               break;
    }
    return "";
}
}
