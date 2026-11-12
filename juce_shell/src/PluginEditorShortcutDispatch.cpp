#include "PluginEditor.h"

// What each command does: the same request a click makes, never a click on a hidden button and never
// the processor directly, so a key cannot do what the button would have refused.
using hypha::shortcuts::Command;
using hypha::observatory::Domain;
using hypha::observatory::ObservationTarget;

void KirinHyphaEditor::performShortcut (Command command)
{
    switch (command)
    {
        case Command::toggleKeys:     toggleKeysList(); return;
        case Command::closeKeys:      closeKeysList(); return;
        case Command::showLevel:      requestObservatoryDomain (Domain::level); return;
        case Command::showTime:       requestObservatoryDomain (Domain::time); return;
        case Command::showFrequency:  requestObservatoryDomain (Domain::frequency); return;
        case Command::showSpace:      requestObservatoryDomain (Domain::space); return;
        case Command::showReference:  requestObservatoryDomain (Domain::reference); return;
        case Command::showAbsolute:
        case Command::showDelta:
            if (! observatoryView.capabilities().targetSelectable) showActionNotice (observatoryView.capabilities().help);
            else requestObservationTarget (command == Command::showDelta ? ObservationTarget::delta : ObservationTarget::absolute);
            return;
        case Command::toggleHybridVu:
            if (processorRef.surroundMeasurementOnly()) showActionNotice ("Mono / stereo only");
            else observatoryView.requestHybridVuToggle();
            return;
        case Command::clearPeakClip:      requestPeakClipClear(); return;
        case Command::resetMeterSession:  requestMeterSessionReset(); return;
        default: break;
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    performComparisonShortcut (command);
   #endif
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::performComparisonShortcut (Command command)
{
    switch (command)
    {
        case Command::startListen:
            if (shortcutContext().listen) showActionNotice ("LISTEN is on");
            else if (! observatoryView.liveCompareFooter().entryEnabled)
                showActionNotice (*liveCompareEntryRefusal (false) != 0 ? liveCompareEntryRefusal (false) : "LISTEN could not start");
            else requestListenStart();
            break;
        case Command::listenPre:     requestListenSelect (true); break;
        case Command::listenPost:    requestListenSelect (false); break;
        case Command::matchListen:   requestListenMatch(); break;
        case Command::chooseFollow:  requestListenAutoMenu(); break;
        case Command::endListen:     requestListenEnd(); break;
        case Command::startBlind:     requestBlindStart(); break;
        case Command::blindSourceOne: requestBlindSource (1); break;
        case Command::blindSourceTwo: requestBlindSource (2); break;
        case Command::revealBlind:    requestBlindReveal(); break;
        case Command::endBlind:       requestBlindEnd(); break;
        case Command::referenceA:     showActionNotice (referenceView.requestRole (0)); break;
        case Command::referenceB:     showActionNotice (referenceView.requestRole (3)); break;
        case Command::referenceC:     showActionNotice (referenceView.requestRole (2)); break;
        case Command::referenceV:     showActionNotice (referenceView.requestRole (1)); break;
        case Command::endReference:   showActionNotice (referenceView.requestRole (0)); break;
        case Command::matchReference: showActionNotice (referenceView.requestCheckMatch()); break;
        case Command::previousSong:    showActionNotice (referenceView.stepSong (-1)); break;
        case Command::nextSong:        showActionNotice (referenceView.stepSong (1)); break;
        case Command::previousVersion: showActionNotice (referenceView.stepVersion (-1)); break;
        case Command::nextVersion:     showActionNotice (referenceView.stepVersion (1)); break;
        case Command::previousTab:     showActionNotice (referenceView.stepTab (-1)); break;
        case Command::nextTab:         showActionNotice (referenceView.stepTab (1)); break;
        case Command::previousCue:     showActionNotice (referenceView.stepCue (-1)); break;
        case Command::nextCue:         showActionNotice (referenceView.stepCue (1)); break;
        case Command::previousSet:     showActionNotice (referenceView.stepSet (-1)); break;
        case Command::nextSet:         showActionNotice (referenceView.stepSet (1)); break;
        case Command::previousTimePage: stepTimePage (-1); break;
        case Command::nextTimePage:     stepTimePage (1); break;
        case Command::returnNormalLevel:
        {
            // RETURN is offered only while POST is held down and LISTEN has ended (View::layoutHeldAttenuation).
            const auto& footer = observatoryView.liveCompareFooter();
            if (footer.postHeldTenthsDb >= 0) showActionNotice ("POST is at its normal level");
            else if (footer.finishing) showActionNotice ("Ended; POST back to normal within 0.5 s");
            else if (footer.active) showActionNotice ("End the current comparison first");
            else requestNormalLevelReturn();
            break;
        }
        case Command::startCandidateCompare:
        {
            // The same two menus as the A/B button: PRE2, then the placement. The key never starts A/B.
            const auto context = shortcutContext();
            if (context.candidateCompareActive) showActionNotice ("A/B is on");
            else if (context.listen) showActionNotice ("End the current comparison first");
            else if (! observatoryView.liveCompareFooter().entryEnabled)
                showActionNotice (*liveCompareEntryRefusal (false) != 0 ? liveCompareEntryRefusal (false) : "A/B could not start");
            else showCandidateCompareMenu();
            break;
        }
        case Command::candidateA:          requestListenSelect (true); break;
        case Command::candidateB:          requestListenSelect (false); break;
        case Command::endCandidateCompare: requestListenEnd(); break;
        default: break;
    }
}
#endif
