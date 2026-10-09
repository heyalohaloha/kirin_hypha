#pragma once
#include "EditorProductChecks.h"
#include <cstring>

namespace hypha::tests::editor_product
{
struct HiddenAutoAccess {};
void enableHiddenAuto (KirinHyphaEditor&, HiddenAutoAccess);
bool hiddenAutoOn (const KirinHyphaEditor&, HiddenAutoAccess);
bool hiddenAutoNotice (const KirinHyphaEditor&, HiddenAutoAccess);
template <auto Member> struct HiddenAutoMember
{
    friend void enableHiddenAuto (KirinHyphaEditor& e, HiddenAutoAccess) { (e.*Member).on = true; }
    friend bool hiddenAutoOn (const KirinHyphaEditor& e, HiddenAutoAccess) { return (e.*Member).on; }
    friend bool hiddenAutoNotice (const KirinHyphaEditor& e, HiddenAutoAccess)
    { return (e.*Member).hiddenStopNoticePending; }
};
template struct HiddenAutoMember<&KirinHyphaEditor::liveCompareAuto>;
struct HiddenTimer
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (HiddenTimer);
};
template struct PrivateAccess<HiddenTimer, &KirinHyphaEditor::timerCallback>;
struct HiddenToast
{
    using Type = juce::String KirinHyphaEditor::*;
    friend Type privateMember (HiddenToast);
};
template struct PrivateAccess<HiddenToast, &KirinHyphaEditor::toastText>;
struct HiddenToastUntil
{
    using Type = double KirinHyphaEditor::*;
    friend Type privateMember (HiddenToastUntil);
};
template struct PrivateAccess<HiddenToastUntil, &KirinHyphaEditor::toastUntil>;

inline void verifyHiddenAutoStops()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.prepareToPlay (48'000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditor());
    auto* editor = dynamic_cast<KirinHyphaEditor*> (owner.get());
    require (editor != nullptr, "shipping editor for hidden AUTO fixture");
    const auto before = processor.liveCompareStatus();
    enableHiddenAuto (*editor, {});
    editor->setVisible (false);
    for (int n = 0; n < 5; ++n) (editor->*privateMember (HiddenTimer {}))();
    require (! hiddenAutoOn (*editor, {}) && hiddenAutoNotice (*editor, {}),
             "hidden timer stops AUTO and retains its notice across hidden ticks");
    const auto after = processor.liveCompareStatus();
    require (std::memcmp (&after.gain, &before.gain, sizeof (after.gain)) == 0
             && std::memcmp (&after.postTarget, &before.postTarget, sizeof (after.postTarget)) == 0,
             "stopping hidden AUTO preserves the approved PRE gain and POST attenuation");
    editor->*privateMember (HiddenToast {}) = "Capture could not be attached";
    editor->*privateMember (HiddenToastUntil {}) = juce::Time::getMillisecondCounterHiRes() * 0.001 + 3.0;
    editor->setVisible (true);
    (editor->*privateMember (HiddenTimer {}))();
    require (hiddenAutoNotice (*editor, {})
             && editor->*privateMember (HiddenToast {}) == "Capture could not be attached",
             "hidden AUTO notice waits rather than overwriting explicit Capture failure");
    editor->*privateMember (HiddenToastUntil {}) = 0.0;
    (editor->*privateMember (HiddenTimer {}))();
    require (! hiddenAutoOn (*editor, {}) && ! hiddenAutoNotice (*editor, {})
             && editor->*privateMember (HiddenToast {}) == "AUTO stopped: editor hidden",
             "visible timer delivers the queued notice without restarting AUTO");
    editor->setVisible (false);
    (editor->*privateMember (HiddenTimer {}))();
    require (! hiddenAutoNotice (*editor, {}), "hiding without AUTO adds no stop notice");
    processor.editorBeingDeleted (owner.get());
    owner.reset();
    processor.releaseResources();
}
}
