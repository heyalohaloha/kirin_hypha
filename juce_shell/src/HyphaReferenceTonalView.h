#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceVisualTimeline.h"

namespace hypha::reference_ui
{
class TonalView final : public juce::Component
{
public:
    struct Curve
    {
        const float* values = nullptr;
        std::uint64_t validBits = 0;
    };

    TonalView();

    void update (std::shared_ptr<const reference_audition::VisualTimeline>,
                 presentation::Context, bool concealed,
                 juce::String candidateName, juce::String cueLabel);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    Curve aCurve() const noexcept;
    Curve cCurve() const noexcept;
    int bandAt (juce::Point<float>) const noexcept;
    int groupAt (juce::Point<float>) const noexcept;
    int activeBand() const noexcept;
    void updateAccessibleDescription();

    std::shared_ptr<const reference_audition::VisualTimeline> timeline;
    presentation::Context context = presentation::defaultContext();
    juce::String candidate, cue;
    juce::Rectangle<float> graphArea, summaryArea;
    int pointedBand = -1, selectedBand = -1;
    bool hidden = false, compact = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TonalView)
};
}
