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
                 presentation::Context, bool concealed, std::shared_ptr<reference_audition::VisualPreferences> = {},
                 juce::String emptyMessage = "Choose Version");
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::Range<double> selectedRange() const noexcept { return { start, end }; }
    std::function<void(double,double)> onCapturedRange;
    // H13: V の画面の Check のタブ。空ならタイムライン（WHOLE）。gainDb は V の追従の gain（鳴っていなければ NaN）。
    // views はその Check の表示（Kirin OS の view_bindings）。帯域の幅と、V で比べられない Check の断りに使う。
    void setSameSection (const juce::String& checkLabel, double gainDb, std::vector<juce::String> views = {});
    const juce::String& sameSectionCheck() const noexcept { return sameSection; }
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
    ViewButton follow { "FOLLOW" }, loudness { "LOUDNESS" }, crest { "CREST" }, tonal { "BALANCE" };
    juce::Image waveformCache;
    juce::Rectangle<float> waveform, graph;
    std::uint64_t cacheRevision = 0;
    juce::String key, captureId;
    juce::String emptyMessage { "Choose Version" };
    juce::String sameSection;
    double sameSectionGain = std::numeric_limits<double>::quiet_NaN();
    std::vector<juce::String> sameSectionViews;
    bool fitCapture = true;
    double position = -1, start = 0, end = 12, dragAnchor = -1, pointedTime = -1;
    bool following = true, showingCrest = false, showingTonal = false, hidden = false;
    std::shared_ptr<reference_audition::VisualPreferences> preferences;
    void saveView();
    void rebuild();
    void rebuildCaptured();
    juce::String capturedValuesAt(double,bool) const;
    void setRange (double, double);
    void publishCapturedRange (bool whole);
    double timeAt (float x) const;
    void paintDetails (juce::Graphics&);
    void paintTonalDetails (juce::Graphics&);
    juce::String valuesAt (double, bool compact = false) const;
};
}
