#pragma once
#include "AttackV2Fixtures.h"

namespace hypha::tests::shell_repairs
{
inline void verifyDrumPaint (const char* lang)
{
    for (const int width : { 300, 375, 450, 525, 600, 750, 899, 900 })
        for (int captureReduction : { 0, 32 })
        {
            AttackComponent view;
            view.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
            view.setSize (width, std::max (80, width * 2 / 3 - 125 - captureReduction));
            view.setBand (5);
            const auto raw = attack_v2_test::summary();
            auto wave = std::make_unique<KirinAttackWaveformBatch>();
            require (view.setNavigationV2 (attack_v2_test::header(), { raw.events, raw.events + 8 },
                                         *wave, *wave, 0, true), "accept disposable DRUM navigation");
            require (view.setSummarySnapshotV2 (raw, 0), "accept disposable DRUM summary");
            juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
            { juce::Graphics g (image); view.paintEntireComponent (g, true); }
            savePreview (image, juce::String (lang) + "-drum-body-" + juce::String (width)
                + (captureReduction == 0 ? "" : "-capture"));
        }
}
}
