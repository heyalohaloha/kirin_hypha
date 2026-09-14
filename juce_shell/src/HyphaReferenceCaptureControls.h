#pragma once
#include "HyphaTheme.h"
#include "HyphaPresentationContext.h"
#include "HyphaTextStyle.h"
#include "HyphaReferenceCapturePresentation.h"
namespace hypha::reference_ui
{
class CaptureControls final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CaptureControls(bool compact=false):statusOnly(compact)
    {
        setWantsKeyboardFocus(true);
        setComponentID(compact ? "capture-a-status" : "capture-a-controls");
        for(auto* button:{&action,&cancel,&view}) addAndMakeVisible(*button);
        action.setComponentID("capture-a-action"); cancel.setComponentID("capture-a-cancel"); view.setComponentID("capture-a-view");
        const auto latch=[this] { return CommandBinding{access,presentation.operation,presentation.command}; };
        action.binding=latch;
        cancel.binding=[this] { return CommandBinding{access,presentation.operation,reference_audition::ACaptureAccess::cancel}; };
        action.onClick=[this] { action.invoke(); };
        cancel.onClick=[this] { cancel.invoke(); };
        view.onClick=[this] { if(access && snapshot.held) { access->capturedView=!access->capturedView; update(access,false,context); } };
    }
    void update(std::shared_ptr<reference_audition::ACaptureAccess> next,bool concealed,presentation::Context value)
    {
        context=value; access=concealed ? nullptr : std::move(next); snapshot=access ? access->snapshot() : reference_audition::ACaptureState{};
        presentation=presentCapture(snapshot,access ? access->currentTimingEpoch.load() : 0);
        const bool active=presentation.busy;
        setVisible(access && (!statusOnly || active));
        for(auto* button:{&action,&cancel,&view}) button->setPresentationContext(context);
        action.setButtonText(presentation.action);
        action.setEnabled(access && access->alive && presentation.command!=reference_audition::ACaptureAccess::none);
        action.setTitle(active ? presentation.action : "Capture original DAW input");
        action.setTooltip(active ? "Stop this capture. Live A audio is unchanged." : "Capture A, then play from the beginning. Stop the DAW to keep the captured range.");
        cancel.setVisible(presentation.cancel && !statusOnly); cancel.setTitle("Discard this capture and keep the previous one");
        view.setVisible(presentation.view && !statusOnly);
        view.setButtonText(access && access->capturedView ? "SAVED" : "LIVE");
        view.setTitle("Displayed A: captured or live. Audio A always remains live.");
        setTitle(concealed ? juce::String() : presentation.primary);
        setDescription(concealed ? juce::String() : presentation.detail);
        setTooltip(concealed ? juce::String() : presentation.detail);
        resized(); repaint();
    }
    int preferredHeight(int width) const { return presentation.secondary.isNotEmpty() ? (width>=550 ? 40 : 32) : 24; }
    void resized() override
    {
        auto area=getLocalBounds();
        if(statusOnly) { action.setBounds(area); cancel.setVisible(false); view.setVisible(false); return; }
        action.setBounds(area.removeFromRight(80));
        if(cancel.isVisible()) { area.removeFromRight(3); cancel.setBounds(area.removeFromRight(58)); }
        if(view.isVisible()) { area.removeFromRight(3); view.setBounds(area.removeFromRight(52)); }
        label=area.reduced(3,0); secondary={};
        if(presentation.secondary.isNotEmpty()) secondary=label.removeFromBottom(getHeight()/2);
    }
    void paint(juce::Graphics& g) override
    {
        if(statusOnly || !access) return;
        g.setColour(COL_TEXT_SECONDARY);
        g.setFont(labelFont(context,typography::TextRole::captureMetadata,typography::Composition::information));
        text_style::drawEllipsized(g,presentation.primary,label,juce::Justification::centredLeft);
        if(!secondary.isEmpty()) text_style::drawEllipsized(g,getWidth()>=550 ? presentation.secondary : presentation.compactSecondary,secondary,juce::Justification::centredLeft);
    }
private:
    struct CommandBinding
    {
        std::shared_ptr<reference_audition::ACaptureAccess> access;
        std::uint64_t operation=0;
        reference_audition::ACaptureAccess::Command command=reference_audition::ACaptureAccess::none;
    };
    class CaptureButton final : public juce::TextButton {
    public:
        explicit CaptureButton(const char* text):juce::TextButton(text) {}
        std::function<CommandBinding()> binding;
        void mouseDown(const juce::MouseEvent& e) override
        {
            ignoredMouseGesture=e.getNumberOfClicks()>1;
            if(ignoredMouseGesture) return;
            if(binding) held=binding();
            juce::TextButton::mouseDown(e);
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            if(ignoredMouseGesture) { ignoredMouseGesture=false; held.reset(); setState(juce::Button::buttonNormal); return; }
            juce::TextButton::mouseUp(e);
        }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if(binding && (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)))
            {
                if(!keyHeld && isEnabled()) { keyHeld=true; held=binding(); invoke(); }
                return true;
            }
            return juce::TextButton::keyPressed(key);
        }
        bool keyStateChanged(bool down) override
        {
            if(!down) keyHeld=false;
            return juce::TextButton::keyStateChanged(down);
        }
        void focusLost(FocusChangeType cause) override { keyHeld=false; juce::TextButton::focusLost(cause); }
        void invoke()
        {
            const auto command=held ? *held : binding ? binding() : CommandBinding{};
            held.reset();
            if(command.access && command.command!=reference_audition::ACaptureAccess::none)
                command.access->request(command.command,command.operation);
        }
        void setPresentationContext(presentation::Context value) { context=value; }
        void paintButton(juce::Graphics& g,bool over,bool down) override {
            const auto bounds=getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(kFieldFill.interpolatedWith(COL_MUTED,down ? 0.30f : over ? 0.18f : 0.04f)); g.fillRoundedRectangle(bounds,3);
            g.setColour(COL_MUTED.withAlpha(0.30f)); g.drawRoundedRectangle(bounds,3,0.6f);
            g.setColour(COL_TEXT_SECONDARY.withAlpha(isEnabled() ? 1.0f : 0.4f));
            g.setFont(labelFont(context,typography::TextRole::captureMetadata,typography::Composition::information));
            g.drawText(getButtonText(),getLocalBounds().reduced(3,0),juce::Justification::centred);
        }
    private: bool keyHeld=false,ignoredMouseGesture=false; std::optional<CommandBinding> held; presentation::Context context=presentation::defaultContext();
    };
    bool statusOnly=false;
    std::shared_ptr<reference_audition::ACaptureAccess> access;
    reference_audition::ACaptureState snapshot;
    presentation::Context context=presentation::defaultContext();
    CaptureButton action{"CAPTURE A"},cancel{"CANCEL"},view{"SAVED"};
    juce::Rectangle<int> label,secondary;
    CapturePresentation presentation;
};
}
