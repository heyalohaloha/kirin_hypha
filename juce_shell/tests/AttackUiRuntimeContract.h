#pragma once
#include "../src/HyphaAttackSnapshotEquality.h"

namespace hypha::attack_ui_test
{
inline bool verifyRedrawContract (const KirinAttackEventBatch& events,
    const KirinAttackWaveformBatch& waveform, const KirinAttackDetailBatch& details,
    const KirinAttackPairEventBatch& pairs, const KirinAttackStats& stats)
{
    auto component = std::make_unique<AttackComponent>();
    auto post = std::make_unique<KirinAttackDetailBatch> (details);
    auto pre = std::make_unique<KirinAttackDetailBatch> (details);
    const auto submit = [&] {
        return component->setSnapshot (events, waveform, *post, waveform, *pre, pairs,
                                       288'000, 48'000, 7, stats); };
    if (! submit() || submit()) return false;
    post->details[1].shape[31] += .001f;
    if (! submit() || submit()) return false;
    pre->details[1].sharpness_acum += .01f;
    if (! submit() || submit()) return false;
    post->details[1].sharpness_acum = std::numeric_limits<float>::quiet_NaN();
    if (! submit() || submit()) return false;
    post->details[1].reserved = 91;
    post->details[1].reserved2 = 551;
    if (submit()) return false;
    component->presentationTick (false);
    if (submit()) return false;
    component->clearSnapshot();
    if (! submit()) return false;
    auto a = details.details[0], b = a;
    a.event_sample = INT64_C(9007199254740992); b.event_sample = a.event_sample + 1;
    return ! attack_equality::same (a, b);
}
}
