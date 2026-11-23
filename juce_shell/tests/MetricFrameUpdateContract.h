#pragma once
#include "../src/HyphaMetricNeedle.h"

namespace hypha::tests
{
template<class Check>
void verifyMetricFrameUpdate (Check check)
{
    using namespace metric_needle;
    Frame frame;
    frame.face = Face::crest;
    frame.status = Status::ready;
    frame.song = "same title";
    frame.reason = "no_valid_value";
    frame.context.referenceIdentity = "reference";
    frame.context.referenceRevision = 6;
    auto equal = frame;
    equal.own = -std::nan ("42");
    check (sameFrameForPaint (frame, equal), "distinct NaN encodings are the same missing value");
    const auto changes = [&] (auto change, const char* message) {
        auto updated = frame;
        change (updated);
        check (!sameFrameForPaint (frame, updated) && !sameFrameForPaint (updated, frame), message);
    };
    for (auto field : { &Frame::own, &Frame::pre, &Frame::reference, &Frame::median })
        changes ([&] (auto& next) { next.*field = 0.0; }, "valid zero never matches a missing reading");
    changes ([] (auto& next) { next.band.low = 0; }, "low band change repaints");
    changes ([] (auto& next) { next.band.high = 0; }, "high band change repaints");
    changes ([] (auto& next) { next.face = Face::psr; }, "face change repaints");
    changes ([] (auto& next) { next.ownIsPre = true; }, "role change repaints");
    changes ([] (auto& next) { next.status = Status::measuring; }, "state change repaints");
    changes ([] (auto& next) { next.reason = "preparing"; }, "reason change repaints");
    changes ([] (auto& next) { next.song = "new title"; }, "title change repaints");
    changes ([] (auto& next) { next.context.referenceIdentity = "new reference"; }, "same-name Reference swap repaints");
    changes ([] (auto& next) { ++next.context.referenceRevision; }, "Reference epoch change repaints");
    changes ([] (auto& next) { next.context.playing = true; }, "playback start without new facts repaints");
    changes ([] (auto& next) { ++next.context.playbackRun; }, "new playback run without new facts repaints");
    FrameUpdate admission;
    check (admission.replace (Frame {}), "the first default frame still reaches paint");
    check (admission.replace (frame), "present a new ready frame");
    int repetitions = 0;
    for (int n = 0; n < 1000; ++n)
        repetitions += admission.replace (equal) ? 1 : 0;
    check (repetitions == 0, "production admission rejects all 1000 unchanged updates");
    auto play = equal; play.context.playing = true;
    check (admission.replace (play) && !admission.replace (play) && admission.current().context.playing,
           "one repaint request for observed play, none for subsequent equal updates");
    frame.own = 0.0;
    changes ([] (auto& next) { next.own = -0.0; }, "preserve signed zero until the painter");
    changes ([] (auto& next) { next.own = 0.001; }, "unequal values are not rounded for the cache");
}
}
