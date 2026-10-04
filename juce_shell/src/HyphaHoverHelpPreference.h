#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaLanguage.h"
#include "HyphaReferenceHelp.h"

namespace hypha
{
// User-level presentation preference shared by PRE and POST. The file is read only from JUCE's
// message thread and written only after an explicit menu action; audio and measurement threads do
// not know that this setting exists. Each plug-in binary keeps one small cache and refreshes it at
// most once per second, so changing POST also reaches every open PRE without per-editor I/O.
class HoverHelpPreference final
{
public:
    explicit HoverHelpPreference (juce::File storageFile);

    static HoverHelpPreference& shared();
    static juce::File defaultStorageFile();

    bool isEnabled();
    bool setEnabled (bool enabled);
    void refreshNowForTest();

private:
    void refreshUnlocked();
    bool writeUnlocked (bool enabled);

    juce::File file;
    juce::CriticalSection stateLock;
    bool cachedEnabled = true;
    bool haveRead = false;
    bool sessionOverride = false;
    juce::uint32 lastRefreshMs = 0u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HoverHelpPreference)
};

// One gate suppresses every SettableTooltipClient in the editor, including dynamic Analysis
// explanations, without disabling mouseMove, FREQ inspection, Focus Trail, MARK, or accessibility.
// Components keep their English help; it is shown in the current language here (INV-S40).
class HoverHelpTooltipWindow final : public juce::TooltipWindow
{
public:
    explicit HoverHelpTooltipWindow (juce::Component* parent, int delayMs)
        : juce::TooltipWindow (parent, delayMs)
    {
    }

    // REF の 300% の B・C・V の中は、吹き出しでなく下の状態の行が説明を出す（HyphaReferenceHelp.h）。
    juce::String getTipFor (juce::Component& component) override
    {
        return HoverHelpPreference::shared().isEnabled() && ! reference_ui::help::shownInLine (component)
                 ? i18n::tr (juce::TooltipWindow::getTipFor (component))
                 : juce::String();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HoverHelpTooltipWindow)
};
}
