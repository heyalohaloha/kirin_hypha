#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceVisualTimeline.h"
#include "reference_audition/ReferenceVisualPreferences.h"
namespace hypha::reference_ui
{
class ComparisonView final : public juce::Component
{
public:
    ComparisonView();
    void update (std::shared_ptr<const reference_audition::VisualTimeline>, double,
                 presentation::Context, bool concealed, std::shared_ptr<reference_audition::VisualPreferences> = {});
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::Range<double> selectedRange() const noexcept { return { start, end }; }
private:
    std::shared_ptr<const reference_audition::VisualTimeline> data;
    reference_audition::VisualBinding lastVerifiedView;
    presentation::Context context = presentation::defaultContext();
    class ViewButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        void paintButton (juce::Graphics&, bool, bool) override;
    };
    ViewButton follow { "FOLLOW" }, loudness { "LOUDNESS" }, crest { "CREST" };
    juce::Image waveformCache;
    juce::Rectangle<float> waveform, graph;
    std::uint64_t cacheRevision = 0;
    juce::String key;
    double position = -1, start = 0, end = 12, dragAnchor = -1, pointedTime = -1;
    bool following = true, showingCrest = false, hidden = false;
    std::shared_ptr<reference_audition::VisualPreferences> preferences;
    void saveView();
    void rebuild();
    void setRange (double, double);
    double timeAt (float x) const;
    void paintDetails (juce::Graphics&);
    juce::String valuesAt (double, bool compact = false) const;
};
}
