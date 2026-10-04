// 2026-10-04：A の取り込み（capture）をやめ、生きていた 3 つを小さな部品へ移した。その確かめ。
//  - Blind の枠（BlindSlot）：1 つの POST で VERSION BLIND とローカル Blind のどちらか 1 つだけ。
//  - A の受け渡し（AInputRelay）：Audio Thread で確保しない。観測していない受け口（世代 0）には何も入れない。
//  - 解析（Rust）のフレーム数の上限：最も高い rate の 4 秒は上限（2,097,152 フレーム）を超え、閉じたまま断る
//    （取り込みのメモリの試験の中にあった確かめ）。
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceAInputRelay.h"
#include "../src/reference_audition/ReferenceBlindSlot.h"
#include "reference_rt_probe.h"
#include "kirin_hypha_reference_ffi.h"

#include <vector>

void testReferenceAInput();

void testReferenceAInput()
{
    ref::BlindSlot slot;
    require (slot.reserve (ref::BlindOwner::version) && ! slot.reserve (ref::BlindOwner::local)
                 && ! slot.reserve (ref::BlindOwner::version),
             "one Blind at a time in a POST");
    slot.release (ref::BlindOwner::local);
    require (slot.owner() == ref::BlindOwner::version && ! slot.reserve (ref::BlindOwner::local),
             "another Blind cannot release the owner");
    slot.release (ref::BlindOwner::version);
    require (slot.reserve (ref::BlindOwner::local) && slot.owner() == ref::BlindOwner::local,
             "the other Blind starts once the first is released");
    slot.release (ref::BlindOwner::local);
    require (! slot.reserve (ref::BlindOwner::none), "nobody is not an owner");
    slot.close();
    require (! slot.reserve (ref::BlindOwner::version) && ! slot.reserve (ref::BlindOwner::local),
             "a closed slot gives no Blind");

    ref::VisualObservation observation ([] { return ref::VisualBinding {}; });
    juce::AudioBuffer<float> input (2, 4'800);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < input.getNumSamples(); ++i)
            input.setSample (c, i, 0.25f * std::sin (0.01f * static_cast<float> (i + c)));
    ref::AInputRelay relay;
    for (const bool feed : { true, false })
        for (const bool licensed : { true, false })
            for (const bool playing : { true, false })
            {
                beginReferenceRtProbe();
                relay.observe (observation, input, 48'000, true, playing, true, 1, {}, licensed, feed);
                require (endReferenceRtProbe() == 0, "A's relay never allocates on the audio thread");
            }
    require (! observation.pendingInput(), "nothing reaches a display that is not observing (generation 0)");

    constexpr std::uint32_t highestRate = 768'000, channels = 2;
    const auto frames = static_cast<size_t> (highestRate) * 4;
    std::vector<float> a (frames * channels, 0.1f), b (frames * channels, 0.1f);
    KirinReferenceGainFacts gain {};
    require (frames > 2'097'152 && ! kirin_hypha_analyze_reference_gain (a.data(), b.data(), frames, highestRate, channels, &gain),
             "the gain analysis keeps its frame cap closed at the highest rate");
    std::cout << "Reference A input: Blind slot, RT relay and the analysis frame cap PASS\n";
}
