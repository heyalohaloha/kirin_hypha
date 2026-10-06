#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaPresentationContext.h"
#include "HyphaReferenceHelp.h"
#include "HyphaReferenceComparisonLayout.h"
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
    // V の画面の Check のタブ。空ならタイムライン（WHOLE）。gainDb は V の追従の gain（鳴っていなければ NaN）。
    // views はその Check の表示（Kirin OS の view_bindings）。帯域の幅と、V で比べられない Check の断りに使う。
    // listening：耳で聴き比べる Check（図の代わりに聴き比べの案内）。
    void setSameSection (const juce::String& checkLabel, double gainDb, std::vector<juce::String> views = {}, bool listening = false);
    const juce::String& sameSectionCheck() const noexcept { return sameSection; }
    // 指した場所の説明（HyphaReferenceHelp.h）。V の Check のタブは描いたときに添えた説明、WHOLE は曲全体の説明。
    juce::String helpAt (juce::Point<int> local) const;
    const comparison_layout::Layout& visualLayout() const noexcept { return viewLayout; }
private:
    std::vector<help::Region> helpRegions;
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
    comparison_layout::Layout viewLayout;
    std::uint64_t cacheRevision = 0;
    juce::String key;
    juce::String emptyMessage { "Choose Version" };
    juce::String sameSection;
    double sameSectionGain = std::numeric_limits<double>::quiet_NaN();
    std::vector<juce::String> sameSectionViews;
    bool sameSectionListening = false;
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
