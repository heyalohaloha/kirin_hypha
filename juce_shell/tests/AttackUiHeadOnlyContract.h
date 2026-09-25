#pragma once

#include "AttackUiLaneContract.h"

#include <iostream>

namespace hypha::attack_ui_test
{
// A hit arrives with its head first (B-1024): the 30 ms head is measured, the body is not final
// yet (or its audio stopped), and the shape covers only the part measured so far.
inline void makeHeadOnly (KirinAttackDetail& detail, std::int64_t measuredBodyBins)
{
    const auto headEnd = detail.event_sample / 48 * 48 + attack_ui::headBins * 48;
    detail.complete = 0;
    detail.transient_available = 0;
    detail.sharpness_available = 0;
    detail.body_end_sample = headEnd;
    detail.shape_end_sample = headEnd + measuredBodyBins * 48;
}

// STRENGTH and CREST are values; TRANSIENT and SHARPNESS are "--", never NEXT HIT or QUIET AFTER;
// the hit can be selected, paired or not.
inline bool verifyHeadOnlyCells()
{
    using namespace attack_lanes;
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    makeHeadOnly (fixture.post->details[1], 12);
    makeHeadOnly (fixture.pre->details[2], 12);
    auto model = std::make_unique<Model>();
    const auto reason = [&model] (std::size_t hit, Lane lane) {
        return model->hits[hit].cells[index (lane)].reason; };
    const auto headOnly = [&] (std::size_t hit) {
        return model->hits[hit].selectable
            && reason (hit, Lane::strength) == Reason::value
            && reason (hit, Lane::crest) == Reason::value
            && reason (hit, Lane::transient) == Reason::missing
            && reason (hit, Lane::sharpness) == Reason::missing; };
    build (*model, *fixture.pairs, *fixture.post, *fixture.pre, 7, 48'000);
    if (! model->delta || ! headOnly (1) || ! headOnly (2)
        || reason (0, Lane::transient) != Reason::value)
    {
        std::cerr << "paired head-only hit is not STRENGTH/CREST only\n";
        return false;
    }
    fixture.pairs->status = KIRIN_SPECTRUM_NO_PAIR;
    build (*model, *fixture.pairs, *fixture.post, *fixture.pre, 7, 48'000);
    if (model->delta || ! headOnly (1) || reason (2, Lane::transient) != Reason::value)
    {
        std::cerr << "POST head-only hit is not STRENGTH/CREST only\n";
        return false;
    }
    return true;
}

// The loupe keeps its 150 ms axis and draws each shape only where it was measured: nothing past
// the measured end changes with the shape values.
inline bool verifyHeadOnlyLoupe()
{
    auto scene = laneScene (872, 412, presentation::forEditor (900, 600));
    auto fixture = laneFixture ({ 192'000 });
    makeHeadOnly (fixture.post->details[0], 12);
    makeHeadOnly (fixture.pre->details[0], 12);
    fixture.submit (*scene.component);
    const auto base = renderAttack (*scene.component);
    for (auto* batch : { fixture.post.get(), fixture.pre.get() })
        for (auto& point : batch->details[0].shape) point *= 0.25f;
    fixture.submit (*scene.component);
    const auto quieter = renderAttack (*scene.component);
    const auto loupe = rectangle (attack_ui::loupeArea (scene.layout));
    // The measured span ends 62 of 150 bins into the axis; the rest of the loupe stays unchanged.
    const auto unmeasured = loupe.withTrimmedLeft (loupe.getWidth() * 11 / 20);
    if (differences (base, quieter, loupe) < 40 || differences (base, quieter, unmeasured) != 0)
    {
        std::cerr << "loupe draws a shape outside its measured span\n";
        return false;
    }
    return writePreviewTo ("KIRIN_ATTACK_UI_HEAD_ONLY_PREVIEW_PATH", base);
}
}
