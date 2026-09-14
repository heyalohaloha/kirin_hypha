#pragma once
#include "HyphaTheme.h"
#include "HyphaPresentationContext.h"
#include "HyphaTextStyle.h"
#include "reference_audition/ReferenceACaptureModel.h"
namespace hypha::reference_ui
{
class CaptureControls final : public juce::Component
{
public:
    explicit CaptureControls(bool compact=false):statusOnly(compact)
    {
        setComponentID(compact ? "capture-a-status" : "capture-a-controls");
        for(auto* button:{&action,&cancel,&view}) addAndMakeVisible(*button);
        action.setComponentID("capture-a-action"); cancel.setComponentID("capture-a-cancel"); view.setComponentID("capture-a-view");
        action.onClick=[this] { if(access) access->request(access->active
            ? (snapshot.phase==reference_audition::ACapturePhase::armed ? reference_audition::ACaptureAccess::cancel : reference_audition::ACaptureAccess::finish)
            : reference_audition::ACaptureAccess::start); };
        cancel.onClick=[this] { if(access) access->request(reference_audition::ACaptureAccess::cancel); };
        view.onClick=[this] { if(access && snapshot.held) { access->capturedView=!access->capturedView; update(access,false,context); } };
    }
    void update(std::shared_ptr<reference_audition::ACaptureAccess> next,bool concealed,presentation::Context value)
    {
        context=value; access=concealed ? nullptr : std::move(next); snapshot=access ? access->snapshot() : reference_audition::ACaptureState{};
        const bool active=access && access->active;
        setVisible(access && (!statusOnly || active));
        for(auto* button:{&action,&cancel,&view}) button->setPresentationContext(context);
        const bool armed=snapshot.phase==reference_audition::ACapturePhase::armed;
        action.setButtonText(active ? (armed ? "CANCEL" : "FINISH A") : snapshot.held ? "CAPTURE AGAIN" : "CAPTURE A");
        action.setEnabled(access && access->alive && access->pending==0);
        action.setTitle(active ? (armed ? "Cancel Capture A" : "Finish Capture A") : "Capture original DAW input");
        action.setTooltip(active ? "Stop this capture. Live A audio is unchanged." : "Capture A, then play from the beginning. Stop the DAW to keep the captured range.");
        cancel.setVisible(active && !armed && !statusOnly); cancel.setTitle("Discard this capture and keep the previous one");
        view.setVisible(snapshot.held && !active && !statusOnly);
        view.setButtonText(access && access->capturedView ? "CAPTURED" : "LIVE");
        view.setTitle("Displayed A: captured or live. Audio A always remains live.");
        const auto data=snapshot.shown;
        const auto seconds=data ? data->duration() : 0;
        status=active ? (armed ? "PLAY" : "CAPTURING") : snapshot.phase==reference_audition::ACapturePhase::partial ? "PARTIAL" : data ? "CAPTURED" : juce::String();
        const bool differs=std::find(snapshot.unitStatus.begin(),snapshot.unitStatus.end(),std::uint8_t(2))!=snapshot.unitStatus.end();
        bool currentDifference=false;
        for(size_t i=0;i<snapshot.unitStatus.size() && i<snapshot.unitPass.size();++i)
            currentDifference=currentDifference || (snapshot.unitStatus[i]==2 && snapshot.unitPass[i]==snapshot.observationPass);
        if(!active && differs) status=snapshot.observationFresh && currentDifference && access && snapshot.confirmedTimingEpoch==access->currentTimingEpoch.load() ? "A DIFFERS" : "LAST CHECK: A DIFFERS";
        if(seconds>0) status+="  "+juce::String(int(seconds)/60)+":"+juce::String(int(seconds)%60).paddedLeft('0',2);
        setTitle(concealed ? juce::String() : status);
        setDescription(concealed ? juce::String() : snapshot.message);
        resized(); repaint();
    }
    void resized() override
    {
        auto area=getLocalBounds();
        if(statusOnly) { action.setBounds(area); cancel.setVisible(false); view.setVisible(false); return; }
        action.setBounds(area.removeFromRight(snapshot.held && !(access && access->active) ? 108 : 86));
        if(cancel.isVisible()) { area.removeFromRight(3); cancel.setBounds(area.removeFromRight(58)); }
        if(view.isVisible()) { area.removeFromRight(3); view.setBounds(area.removeFromRight(76)); }
        label=area.reduced(3,0);
    }
    void paint(juce::Graphics& g) override
    {
        if(statusOnly || !access) return;
        g.setColour(COL_TEXT_SECONDARY);
        g.setFont(labelFont(context,typography::TextRole::captureMetadata,typography::Composition::information));
        text_style::drawEllipsized(g,snapshot.message.isNotEmpty() ? snapshot.message : status,label,juce::Justification::centredLeft);
    }
private:
    class CaptureButton final : public juce::TextButton {
    public:
        explicit CaptureButton(const char* text):juce::TextButton(text) {}
        void setPresentationContext(presentation::Context value) { context=value; }
        void paintButton(juce::Graphics& g,bool over,bool down) override {
            const auto bounds=getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(kFieldFill.interpolatedWith(COL_MUTED,down ? 0.30f : over ? 0.18f : 0.04f)); g.fillRoundedRectangle(bounds,3);
            g.setColour(COL_MUTED.withAlpha(0.30f)); g.drawRoundedRectangle(bounds,3,0.6f);
            g.setColour(COL_TEXT_SECONDARY.withAlpha(isEnabled() ? 1.0f : 0.4f));
            g.setFont(labelFont(context,typography::TextRole::captureMetadata,typography::Composition::information));
            g.drawText(getButtonText(),getLocalBounds().reduced(3,0),juce::Justification::centred);
        }
    private: presentation::Context context=presentation::defaultContext();
    };
    bool statusOnly=false;
    std::shared_ptr<reference_audition::ACaptureAccess> access;
    reference_audition::ACaptureState snapshot;
    presentation::Context context=presentation::defaultContext();
    CaptureButton action{"CAPTURE A"},cancel{"CANCEL"},view{"CAPTURED"};
    juce::Rectangle<int> label;
    juce::String status;
};
}
