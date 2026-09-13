#include "PluginEditor.h"

namespace
{
    namespace ui = hypha::ui_contract;
    juce::String allKeepMenuLabel (int nReady)
    {
        return juce::String ("All Keep: ") + juce::String (nReady) + " ready POST"
             + (nReady == 1 ? "" : "s");
    }

    bool claimedByOtherPost (const KirinHyphaProcessorBase::PreCandidate& candidate,
                             const juce::String& ownInstanceId,
                             const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& c : claims)
            if (c.instanceId != ownInstanceId
                && ((c.hasPairedPreInstanceId && c.pairedPreInstanceId == candidate.instanceId)
                    || (! c.hasPairedPreInstanceId && c.hasPairPreName && candidate.hasName
                        && candidate.name.isNotEmpty() && c.pairPreName == candidate.name)))
                return true;
        return false;
    }

    juce::String resolvedOwnPreInstanceId (
        const juce::String& ownInstanceId,
        const juce::String& latchedPreInstanceId,
        const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& claim : claims)
            if (claim.instanceId == ownInstanceId && claim.hasPairedPreInstanceId)
                return claim.pairedPreInstanceId;
        return latchedPreInstanceId;
    }

    juce::String meterContextLabel (hypha::meter_context::MeterContext context)
    {
        return context == hypha::meter_context::MeterContext::trackStem
            ? "TRACK / STEM" : "2MIX";
    }
}

KirinHyphaEditor::PairMenuLookAndFeel& KirinHyphaEditor::pairMenuLookAndFeel()
{
    static PairMenuLookAndFeel lookAndFeel;
    return lookAndFeel;
}

void KirinHyphaEditor::showCandidateMenu()
{
    processorRef.refreshLicenseForUserAction();
    // Built on click; every live PRE remains independently selectable by exact identity.
    const bool playing = processorRef.isPlaying(); // W-280: pair change locked during playback
    // B-115: lock only when playing AND live (processBlock running). A frozen `playing` with a
    // stalled heartbeat does not lock (false-release prevention; signal_state is silence-conflated).
    const bool pairLocked = playing && processorRef.heartbeatLive();
    const auto cands = processorRef.enumeratePreCandidates();
    const auto claims = processorRef.enumeratePostPairClaims();
    const juce::String ownInstanceId = processorRef.instanceId();

    juce::StringArray labels;
    juce::Array<bool> labelEnabled;
    juce::Array<bool> labelChecked;
    const juce::String currentPreInstanceId = resolvedOwnPreInstanceId (
        ownInstanceId, processorRef.pairedPreInstanceId(), claims);
    for (const auto& c : cands)
    {
        const bool selected = currentPreInstanceId.isNotEmpty()
                           && c.instanceId == currentPreInstanceId;
        const bool inUse = claimedByOtherPost (c, ownInstanceId, claims);
        int sameNameCount = 0;
        if (c.hasName && c.name.isNotEmpty())
            for (const auto& other : cands)
                if (other.hasName && other.name == c.name)
                    ++sameNameCount;
        const juce::String shown = c.hasName && c.name.isNotEmpty()
                                     ? c.name + (sameNameCount > 1
                                                     ? " / " + c.instanceId.substring (0, 8)
                                                     : juce::String())
                                     : "PRE " + c.instanceId.substring (0, 8);
        labels.add ((inUse ? "In use by another POST: " : "Use PRE: ") + shown);
        labelEnabled.add (! inUse);
        labelChecked.add (selected && ! inUse);
    }

    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    const bool pairSelected = processorRef.pairStatus() != KIRIN_PAIR_STATUS_UNPAIRED;
    menu.addSectionHeader ("PRE connection");
    if (pairLocked)
        menu.addItem (2, "Stop playback to change connection", false, false);
    menu.addItem (11, "Use POST only", ! pairLocked, ! pairSelected);
    if (cands.isEmpty())
        menu.addItem (3, "No available PRE", false, false); // explicit result of user action
    else
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem (100 + i, labels[i], ! pairLocked && labelEnabled[i], labelChecked[i]);

    const auto options = juce::PopupMenu::Options()
                             .withTargetComponent (&pairDropdown)
                             .withDeletionCheck (*this)
                             .withMinimumWidth (ui::pairMenuMinimumWidth)
                             .withMaximumNumColumns (ui::pairMenuMaximumColumns)
                             .withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    menu.showMenuAsync (options, [safeThis, candidates = cands] (int result)
    {
        if (safeThis != nullptr)
            safeThis->handleCandidateMenu (result, candidates);
    });
}

void KirinHyphaEditor::showOperationsMenu()
{
    processorRef.refreshLicenseForUserAction();
    const bool recording = processorRef.isRecording();
    const bool keepActive = recording
        || processorRef.keepPhase() != (int) KIRIN_KEEP_PHASE_IDLE;
    const bool pairSelected = processorRef.pairStatus() != KIRIN_PAIR_STATUS_UNPAIRED;
    const bool osOwned = processorRef.licenseIsOs();
    const int nReady = processorRef.keepReadyCount();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    if (isPost)
    {
        menu.addSectionHeader ("Keep");
        if (keepActive)
        {
            menu.addItem (5, "Stop selected pair");
            menu.addItem (2, "All Stop: active POSTs");
        }
        else
        {
            menu.addItem (4, "Keep selected pair", osOwned && pairSelected);
            menu.addItem (1, osOwned ? allKeepMenuLabel (nReady)
                                     : "All Keep: Kirin OS required",
                          osOwned && nReady >= 1);
        }
        menu.addSeparator();
    }
    menu.addSectionHeader ("Measurement");
    menu.addItem (20, "Reset Meter Session");
    if (isPost)
    {
        if (recording) menu.addItem (22, "Add NOTE at current position", osOwned);
        if (observatoryDomain != hypha::observatory::Domain::reference)
            menu.addItem (21, "Create Capture");
        if (observatoryView.localBlindEntryAvailable())
            menu.addItem (23, (getWidth() < 900 ? juce::String ("PRE / POST Blind / Open at 300% / ")
                                              : juce::String ("PRE / POST Blind Compare / "))
                              + meterContextLabel (processorRef.meterContextPreference()));
        else if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_AAX)
            menu.addItem (23, "PRE / POST Blind / AAX validation pending", false);
    }
    menu.addSeparator();
    menu.addSectionHeader ("Display");
    menu.addItem (11, "Show Hybrid VU while recording", true,
                  processorRef.hybridVuOnRecordPreference());
    if (observatoryView.hybridVuShownByRecording())
        menu.addItem (12, "Show selected view for this recording");
    menu.addItem (10, "Show hover help", true,
                  hypha::HoverHelpPreference::shared().isEnabled());
    if (appearanceSnapshot.activationSeen)
        menu.addItem (jungleModeMenuAction, "Jungle Mode", true,
                      observatoryView.jungleAppearanceEnabled());
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.operationsMenuAnchor())
        .withDeletionCheck (*this).withMinimumWidth (juce::jlimit (300, 420, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (options, [safe] (int result)
    { if (safe != nullptr) safe->handleOperationsMenu (result); });
}

void KirinHyphaEditor::handleOperationsMenu (int result)
{
    if (result == 20)
    {
        if (observatoryView.onReset) observatoryView.onReset();
    }
    else if (result == 21)
    {
        if (observatoryView.onCapture) observatoryView.onCapture();
    }
    else if (result == 22)
    {
        if (observatoryView.onNote) observatoryView.onNote();
    }
    else if (result == 23)
    {
        if (observatoryView.onLocalBlind) observatoryView.onLocalBlind();
    }
    else if (result == 11 || result == 12)
        handleInformationMenu (result);
    else
        handleCandidateMenu (result, {});
}

void KirinHyphaEditor::showMeterContextMenu (juce::Component& anchor)
{
    const auto current = processorRef.meterContextPreference();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Meter context");
    menu.addItem (700, "2MIX / Mix or master bus / continuous sections",
                  true, current == hypha::meter_context::MeterContext::twoMix);
    menu.addItem (701, "TRACK / STEM / individual or group bus / sparse events",
                  true, current == hypha::meter_context::MeterContext::trackStem);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&anchor).withDeletionCheck (*this)
        .withMinimumWidth (juce::jlimit (300, 500, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (options, [safe] (int result)
    {
        if (safe == nullptr || (result != 700 && result != 701)) return;
        safe->applyMeterContextChoice (result == 701
            ? hypha::meter_context::MeterContext::trackStem
            : hypha::meter_context::MeterContext::twoMix);
    });
}

void KirinHyphaEditor::applyMeterContextChoice (hypha::meter_context::MeterContext context)
{
    if (processorRef.meterContextPreference() == context) return;
    const auto scale = hypha::meter_context::initialScaleFor (context);
    observatoryView.setMeterContext (context);
    observatoryView.setScaleMode (scale);
    processorRef.setMeterContextPreference (context);
    processorRef.setScaleModePreference (scale);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    localBlindView.setMeterContext (context);
    if (analysisPage == AnalysisPage::attack
        && ! hypha::meter_context::drumAttackAvailable (context))
        setAnalysisPage (AnalysisPage::meters);
    updateTimePageNavigation();
    if (localBlindOpen) refreshLocalBlindProduct();
   #endif
}

void KirinHyphaEditor::showDomainMenu()
{
    const auto role = isPost ? hypha::observatory::Role::post
                             : hypha::observatory::Role::pre;
    constexpr std::array domains {
        hypha::observatory::Domain::level, hypha::observatory::Domain::time,
        hypha::observatory::Domain::frequency, hypha::observatory::Domain::space,
        hypha::observatory::Domain::reference
    };
    constexpr std::array labels { "LEVEL", "TIME", "FREQ", "SPACE", "REF" };
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Observation view");
    for (size_t index = 0; index < domains.size(); ++index)
        menu.addItem (300 + (int) index, labels[index],
                      hypha::observatory::domainCapabilities (role).allows (domains[index]),
                      observatoryDomain == domains[index]);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.domainMenuAnchor())
        .withDeletionCheck (*this).withMinimumWidth (220).withMaximumNumColumns (1)
        .withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (options, [safe] (int result)
    {
        if (safe == nullptr || result < 300 || result >= 305) return;
        safe->setObservatoryDomain (
            static_cast<hypha::observatory::Domain> (result - 300));
    });
}

void KirinHyphaEditor::showSizeMenu()
{
    tooltip.hideTip();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Editor size");
    for (size_t index = 0; index < hypha::observatory::sizePresets.size(); ++index)
    {
        const auto& preset = hypha::observatory::sizePresets[index];
        const auto label = juce::String (preset.label) + "   "
                         + juce::String (preset.width) + juce::String::fromUTF8 ("×")
                         + juce::String (preset.height);
        menu.addItem (400 + (int) index, label, true,
                      getWidth() == preset.width && getHeight() == preset.height);
    }
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.sizeMenuAnchor())
        .withDeletionCheck (*this).withMinimumWidth (240).withMaximumNumColumns (1)
        .withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (options, [safe] (int result)
    {
        if (safe == nullptr || result < 400 || result >= 405) return;
        const auto preset = hypha::observatory::sizePresets[(size_t) (result - 400)];
        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::callAsync ([safe, preset]
        {
            if (safe != nullptr
                && (safe->getWidth() != preset.width || safe->getHeight() != preset.height)
                && safe->observatoryView.onSizeChange)
                safe->observatoryView.onSizeChange (preset);
        });
    });
}

void KirinHyphaEditor::showGuideInformationMenu()
{
    // The visible guide rail owns both actions. A separate overlaid CONNECT
    // button can retain empty bounds when a request arrives after layout.
    if (processorRef.pendingPreDisplayConnection().validAt (juce::Time::currentTimeMillis()))
    {
        if (! processorRef.acceptPreDisplayConnection())
            showToast (processorRef.licenseIsOs() ? "Connection request is no longer available"
                                                  : "Kirin OS is required for Work connection");
        return;
    }
    const auto display = processorRef.preDisplaySnapshot();
    const auto guide = processorRef.guidePresentationSnapshot();
    if (! guide.guideAvailable && display.primary.isEmpty() && display.detail.isEmpty()) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    const auto heading = guide.payloadKind == "masking" ? "MASKING / OS GUIDE"
                       : guide.payloadKind == "inspect" ? "INSPECT / OS GUIDE" : "OS GUIDE";
    menu.addSectionHeader (heading);
    int item = 500;
    const auto addFact = [&menu, &item] (const juce::String& text)
    { if (text.isNotEmpty()) menu.addItem (item++, text, false); };
    addFact (display.primary);
    addFact (display.detail);
    addFact (display.stateText);
    addFact (guide.sourcePairLabel.isNotEmpty() ? "Sources  " + guide.sourcePairLabel
                                                : juce::String());
    if (guide.hasPrimary)
    {
        addFact (guide.primary.label);
        addFact (guide.primary.sourceLabel.isNotEmpty()
                     ? "Source  " + guide.primary.sourceLabel : juce::String());
        addFact (guide.primary.channel.isNotEmpty()
                     ? "Channel  " + guide.primary.channel : juce::String());
        if (guide.primary.hasBand)
            addFact (juce::String (guide.primary.lowHz, 0) + " to "
                     + juce::String (guide.primary.highHz, 0) + " Hz"
                     + (guide.primary.frequencyBasis.isNotEmpty()
                            ? juce::String (" / ") + guide.primary.frequencyBasis
                            : juce::String()));
    }
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.guideDetailsAnchor())
        .withDeletionCheck (*this).withMinimumWidth (juce::jlimit (300, 440, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight);
    menu.showMenuAsync (options, [] (int) {});
}

void KirinHyphaEditor::showFeedbackInformationMenu()
{
    if (observatoryView.feedback().isEmpty()) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Status");
    menu.addItem (600, observatoryView.feedback(), false);
    menu.showMenuAsync (juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView.feedbackDetailsAnchor())
        .withDeletionCheck (*this).withMinimumWidth (juce::jlimit (300, 440, getWidth()))
        .withMaximumNumColumns (1).withStandardItemHeight (ui::pairMenuItemHeight),
        [] (int) {});
}

void KirinHyphaEditor::handleCandidateMenu (
    int result, const juce::Array<KirinHyphaProcessorBase::PreCandidate>& candidates)
{
    if (result == 1)
    {
        if (! processorRef.keepAll())
        {
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 2)
        processorRef.stopAll();
    else if (result == 4)
    {
        if (! processorRef.keepPair())
        {
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs()
                           ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 5)
        processorRef.stopPair();
    else if (result == 10)
    {
        auto& preference = hypha::HoverHelpPreference::shared();
        const bool enabled = ! preference.isEnabled();
        const bool persisted = preference.setEnabled (enabled);
        if (! enabled)
            tooltip.hideTip();
        if (! persisted)
            showToast ("Hover help changed for this session only");
    }
    else if (result == jungleModeMenuAction)
        requestJungleChoice (! observatoryView.jungleAppearanceEnabled());
    else if (result == 11)
    {
        processorRef.clearPairCandidate();
        pairedPreExplicitlyBypassed = false;
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        spectrumView.clearSnapshot();
        perceptualView.clearSnapshot();
        absoluteView.clearSnapshot();
       #endif
        nameField.setModelName (processorRef.pairDisplayName());
    }
    else if (result >= 100)
    {
        const int idx = result - 100;
        if (idx >= 0 && idx < candidates.size())
        {
            const auto candidate = candidates.getReference (idx);
            const juce::String name = candidate.hasName ? candidate.name : juce::String();
            if (processorRef.setPairCandidate (candidate.instanceId, name))
            {
                pairedPreExplicitlyBypassed = false;
               #if ! KIRIN_HYPHA_PRE_DISPLAY
                spectrumView.clearSnapshot();
                perceptualView.clearSnapshot();
                absoluteView.clearSnapshot();
               #endif
                nameField.setModelName (processorRef.pairDisplayName());
            }
            else
            {
                const juce::String notice = processorRef.drainKeepActionNotice();
                showToast (notice.isNotEmpty() ? notice : juce::String ("PRE no longer available"));
            }
        }
    }
}
