#include "PluginEditor.h"
#include "HyphaBuildIdentity.h"
#include "HyphaLocalBlindUiContract.h"
#include "HyphaPluginFormat.h"
#include "HyphaUpdateContract.h"

namespace
{
namespace update = hypha::update_information;
#if JUCE_DEBUG
constexpr int pdcValidationAction = 900;
#endif
juce::String toString (std::string_view value)
{
    return juce::String::fromUTF8 (value.data(), static_cast<int> (value.size()));
}
}

bool KirinHyphaEditor::informationBlockedByBlind() const
{
    return isPost && (processorRef.referenceAuditionSnapshot().blindPhase
        != hypha::reference_audition::BlindPhase::inactive
        || (processorRef.localBlindProductSupported()
            && hypha::local_blind_ui::blocksDisclosure (processorRef.localBlindProductView())));
}

void KirinHyphaEditor::showInformationMenu()
{
    // UI actions only. In particular, opening this menu never performs a release check.
    if (informationBlockedByBlind())
    {
        showToast ("Hypha information is available after Blind Compare");
        return;
    }
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader (juce::String ("Hypha ") + (isPost ? "POST" : "PRE"));
    menu.addItem (1, juce::String ("Loaded v") + JucePlugin_VersionString, false);
    menu.addItem (2, juce::String (hypha::plugin_format::name (processorRef.wrapperType)) + " / "
                      + juce::SystemStats::getOperatingSystemName(), false);
    menu.addItem (3, juce::String ("Source ") + HYPHA_SOURCE_ID + " / "
                      + HYPHA_SOURCE_STATE, false);
    menu.addItem (4, "Official release identity not verified", false);
   #if JUCE_DEBUG
    juce::PopupMenu validation;
    int diagnosticId = 1000;
    for (const auto& fact : processorRef.localValidationFacts())
        validation.addItem (diagnosticId++, fact, false);
    menu.addSubMenu ("Validation facts (read-only)", validation);
    if (isPost)
    {
        menu.addSectionHeader ("Local Blind host validation");
        menu.addItem (pdcValidationAction, "Capture one exact 4 s PRE/POST range",
                      processorRef.isPlaying());
    }
   #endif
    menu.addSeparator();
    const auto add = [&] (update::Action action, const juce::String& text)
    { menu.addItem (static_cast<int> (action), text); };
    // The current shell has no language setting. Offer both official languages explicitly.
    add (update::Action::downloadsEnglish, "Update information and downloads (English)");
    add (update::Action::downloadsJapanese, juce::String::fromUTF8 ("更新情報とダウンロード（日本語）"));
    add (update::Action::changes, "Release notes");
    juce::PopupMenu copy;
    copy.addItem (static_cast<int> (update::Action::copyEnglish), "Downloads (English)");
    copy.addItem (static_cast<int> (update::Action::copyJapanese), "Downloads (Japanese)");
    copy.addItem (static_cast<int> (update::Action::copyChanges), "Release notes");
    menu.addSubMenu ("Copy official URL", copy);
    menu.addSeparator();
    menu.addSectionHeader ("Updating PRE and POST together");
    menu.addItem (5, "Save work, close the DAW, then install both", false);
    menu.addItem (6, "Restart / rescan; check both loaded versions", false);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (observatoryView.informationAnchor())
        .withDeletionCheck (*this).withMinimumWidth (360)
        .withMaximumNumColumns (1).withStandardItemHeight (26);
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    menu.showMenuAsync (options, [safeThis] (int result)
    {
        if (safeThis != nullptr) safeThis->handleInformationMenu (result);
    });
}

void KirinHyphaEditor::handleInformationMenu (int result)
{
    if (result == 0) return;
    if (result == 11)
    {
        const bool enabled = ! processorRef.hybridVuOnRecordPreference();
        processorRef.setHybridVuOnRecordPreference (enabled);
        if (observatoryView.setHybridVuOnRecordEnabled (enabled))
            resized();
        return;
    }
    if (result == 12)
    {
        if (observatoryView.dismissHybridVuForCurrentRecording()) resized();
        return;
    }
    if (result == jungleModeMenuAction)
    {
        requestJungleChoice (! observatoryView.jungleAppearanceEnabled());
        return;
    }
   #if JUCE_DEBUG
    if (result == pdcValidationAction)
    {
        if (processorRef.startLocalBlindPdcValidation())
            showToast ("Capture scheduled; keep playback running for 5 seconds");
        else
            showToast ("Capture not scheduled; reopen Validation facts for the exact reason");
        return;
    }
   #endif
    const bool busy = informationBlockedByBlind();
    const auto outcome = update::dispatch (static_cast<update::Action> (result), busy,
        [] (std::string_view destination)
        { return juce::URL (toString (destination)).launchInDefaultBrowser(); },
        [] (std::string_view destination)
        { juce::SystemClipboard::copyTextToClipboard (toString (destination)); });
    if (outcome == update::Outcome::hoverHelpRequested)
        handleCandidateMenu (10, {});
    else if (outcome == update::Outcome::blocked)
        showToast ("Available after Blind Compare");
    else if (outcome == update::Outcome::openFailed)
    {
        // A readable, user-action error even at 300x200; do not truncate it into the footer.
        juce::PopupMenu failure;
        failure.setLookAndFeel (&pairMenuLookAndFeel());
        failure.addSectionHeader ("Could not open the browser");
        failure.addItem (static_cast<int> (update::copyAction (
            static_cast<update::Action> (result))), "Copy official URL");
        const juce::Component::SafePointer<KirinHyphaEditor> safe (this);
        failure.showMenuAsync (juce::PopupMenu::Options()
            .withTargetComponent (observatoryView.informationAnchor())
            .withDeletionCheck (*this).withMinimumWidth (300).withStandardItemHeight (28),
            [safe] (int selected)
            { if (safe != nullptr) safe->handleInformationMenu (selected); });
    }
    else if (outcome == update::Outcome::copied)
        showToast ("Official URL copied");
}
