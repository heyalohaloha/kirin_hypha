#pragma once
#include "../src/PluginEditor.h"

namespace hypha::tests
{
template <class Tag, typename Tag::Type Member> struct ShortcutPrivateAccess
{
    friend typename Tag::Type shortcutMember (Tag) { return Member; }
};
struct ShortcutView
{
    using Type = hypha::observatory::View KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutView);
};
template struct ShortcutPrivateAccess<ShortcutView, &KirinHyphaEditor::observatoryView>;
struct ShortcutName
{
    using Type = hypha::EditableName KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutName);
};
template struct ShortcutPrivateAccess<ShortcutName, &KirinHyphaEditor::nameField>;
struct ShortcutResetDialog
{
    using Type = std::unique_ptr<juce::AlertWindow> KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutResetDialog);
};
template struct ShortcutPrivateAccess<ShortcutResetDialog, &KirinHyphaEditor::meterResetDialog>;
struct ShortcutWatchPresent
{
    using Type = bool KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutWatchPresent);
};
template struct ShortcutPrivateAccess<ShortcutWatchPresent, &KirinHyphaEditor::haveObservatoryWatchDisplay>;
struct ShortcutKeysPanel
{
    using Type = hypha::KeysPanel KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutKeysPanel);
};
template struct ShortcutPrivateAccess<ShortcutKeysPanel, &KirinHyphaEditor::keysPanel>;
struct ShortcutTooltip
{
    using Type = hypha::HoverHelpTooltipWindow KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutTooltip);
};
template struct ShortcutPrivateAccess<ShortcutTooltip, &KirinHyphaEditor::tooltip>;
struct ShortcutToastText
{
    using Type = juce::String KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutToastText);
};
template struct ShortcutPrivateAccess<ShortcutToastText, &KirinHyphaEditor::toastText>;
struct ShortcutTimer
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type shortcutMember (ShortcutTimer);
};
template struct ShortcutPrivateAccess<ShortcutTimer, &KirinHyphaEditor::timerCallback>;
#if ! KIRIN_HYPHA_PRE_DISPLAY
struct ShortcutLocalBlindView
{
    using Type = hypha::local_blind_ui::Component KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutLocalBlindView);
};
template struct ShortcutPrivateAccess<ShortcutLocalBlindView, &KirinHyphaEditor::localBlindView>;
struct ShortcutLocalBlindOpen
{
    using Type = bool KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutLocalBlindOpen);
};
template struct ShortcutPrivateAccess<ShortcutLocalBlindOpen, &KirinHyphaEditor::localBlindOpen>;
struct ShortcutReference
{
    using Type = hypha::reference_ui::Component KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutReference);
};
template struct ShortcutPrivateAccess<ShortcutReference, &KirinHyphaEditor::referenceView>;
struct ShortcutLiveBlindView
{
    using Type = hypha::live_blind_ui::Component KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutLiveBlindView);
};
template struct ShortcutPrivateAccess<ShortcutLiveBlindView, &KirinHyphaEditor::liveBlindView>;
struct ShortcutTimePages
{
    using Type = hypha::TimePageNavigation KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutTimePages);
};
template struct ShortcutPrivateAccess<ShortcutTimePages, &KirinHyphaEditor::timePageNavigation>;
struct ShortcutLiveBlindOpen
{
    using Type = bool KirinHyphaEditor::*;
    friend Type shortcutMember (ShortcutLiveBlindOpen);
};
template struct ShortcutPrivateAccess<ShortcutLiveBlindOpen, &KirinHyphaEditor::liveBlindOpen>;
struct ShortcutBlindStart { using Type = void (KirinHyphaEditor::*)(); friend Type shortcutMember (ShortcutBlindStart); };
template struct ShortcutPrivateAccess<ShortcutBlindStart, &KirinHyphaEditor::requestBlindStart>;
struct ShortcutBlindSource { using Type = void (KirinHyphaEditor::*) (int); friend Type shortcutMember (ShortcutBlindSource); };
template struct ShortcutPrivateAccess<ShortcutBlindSource, &KirinHyphaEditor::requestBlindSource>;
struct ShortcutBlindReveal { using Type = void (KirinHyphaEditor::*)(); friend Type shortcutMember (ShortcutBlindReveal); };
template struct ShortcutPrivateAccess<ShortcutBlindReveal, &KirinHyphaEditor::requestBlindReveal>;
struct ShortcutBlindEnd { using Type = void (KirinHyphaEditor::*)(); friend Type shortcutMember (ShortcutBlindEnd); };
template struct ShortcutPrivateAccess<ShortcutBlindEnd, &KirinHyphaEditor::requestBlindEnd>;
#endif
}
