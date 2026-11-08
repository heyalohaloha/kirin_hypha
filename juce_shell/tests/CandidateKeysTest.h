#pragma once
#include "CandidateProductHost.h"

// A/B from the keyboard with real PRE1, PRE2 and POST. W goes through the same two menus as the button,
// so no key can skip the placement; 1, 2 and T are the controls A/B shares with LISTEN.
namespace hypha::tests
{
template<class Require> void verifyCandidateKeys (ProductTestClock& wall, Require require)
{
    std::cout << "Candidate product: keys\n" << std::flush;
    CandidateHost<Require> host (wall, require, 2, 512, false);
    host.pair();
    auto& post = host.post();
    enterCandidateThroughEditor (post, require, {}, nullptr, true);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
    juce::DocumentWindow window ("Keys", juce::Colours::black, juce::DocumentWindow::minimiseButton);
    window.setContentNonOwned (editor.get(), true);
    window.setVisible (true);
    editor->grabKeyboardFocus();
    require (editor->getPeer() != nullptr && editor->isShowing(), "native window for A/B's keys");
    const auto press = [&] (int code)
    {
        const auto used = editor->getPeer()->handleKeyPress (juce::KeyPress (code));
        editor->getPeer()->handleKeyUpOrDown (false);
        return used;
    };
    host.render (100, true, false);
    require (press ('W') && post.candidateCompareActive() && juce::Component::getNumCurrentlyModalComponents() == 0,
             "W during A/B opens nothing and keeps it running");
    require (press ('1') && post.liveCompareStatus().preSelected, "1 chooses A");
    host.render (180, true, true);
    require (press ('2') && ! post.liveCompareStatus().preSelected, "2 chooses B");
    host.render (180, true, false, false);
    require (press ('T') && post.liveCompareStatus().finishing, "T ends A/B");
    host.render (10, true, false, false);
    host.render (810, false, false);
    require (! post.candidateCompareActive() && ! post.candidateMeasurementIsolated() && ! post.liveCompareStatus().active,
             "T returns POST to the ordinary chain");
    window.clearContentComponent();
    post.editorBeingDeleted (editor.get());
    editor.reset();
}
}
