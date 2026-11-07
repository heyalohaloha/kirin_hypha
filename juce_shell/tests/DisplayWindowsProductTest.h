#pragma once

// Included after the shipping processor/editor fixture helpers.
inline void verifyDisplayWindowsProduct()
{
    namespace average = hypha::spectrum_average;
    namespace ref = hypha::reference_audition;
    for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        Processor processor (role);
        const auto save = [&]
        {
            juce::MemoryBlock bytes; processor.getStateInformation (bytes);
            return juce::AudioProcessor::getXmlFromBinary (bytes.getData(), static_cast<int> (bytes.getSize()));
        };
        require (! save()->hasAttribute ("freq_curve_average"), "the shipping processor omits the default average");
        for (const auto option : average::choices)
        {
            processor.setSpectrumAveragePreference (option);
            juce::MemoryBlock bytes; processor.getStateInformation (bytes);
            processor.setSpectrumAveragePreference (average::Choice::live);
            processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            require (processor.spectrumAverage.get() == option, "the processor state round trips each FREQ average");
        }
        for (const auto* value : { "", "NaN", "0.5 S", "999999" })
        {
            auto xml = save();
            xml->setAttribute ("freq_curve_average", value);
            juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
            processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            require (processor.spectrumAverage.get() == average::Choice::live, "invalid average state restores the default");
        }
        juce::XmlElement legacy ("KirinHyphaState");
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (legacy, old);
        processor.setSpectrumAveragePreference (average::Choice::seconds10);
        processor.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
        require (processor.spectrumAverage.get() == average::Choice::live && ! save()->hasAttribute ("freq_curve_average"),
                 "an old project restores LIVE and saves no new default");
    }
    Processor processor (Processor::Role::Post);
    juce::XmlElement xml ("KirinHyphaState");
    ref::ReferenceComparisonSettings settings;
    settings.aWindows.reference = settings.aWindows.check = ref::AWindowChoice::custom;
    settings.aWindows.referenceSeconds = 3; settings.aWindows.checkSeconds = 600;
    settings.write (xml);
    juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary (xml, bytes);
    processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
    processor.getStateInformation (bytes);
    const auto output = juce::AudioProcessor::getXmlFromBinary (bytes.getData(), static_cast<int> (bytes.getSize()));
    const auto windows = ref::ReferenceComparisonSettings::read (*output).aWindows;
    require (windows.blocksOf (3, 50) == 30 && windows.blocksOf (2, 50) == 6000, "the processor retains custom A windows by role");
    require (! processor.referenceAuditionSnapshot().bSelected, "restoring custom display windows never selects audio");

    // Use the actual editor callback to change the processor preference, then reopen it.
    const auto findSpectrum = [] (juce::Component& root, const auto& self) -> hypha::SpectrumComponent*
    {
        if (auto* found = dynamic_cast<hypha::SpectrumComponent*> (&root)) return found;
        for (auto* child : root.getChildren()) if (auto* found = self (*child, self)) return found;
        return nullptr;
    };
    processor.setSpectrumAveragePreference (average::Choice::seconds3);
    processor.setObservatoryDomainPreference (hypha::observatory::stateValue (Domain::frequency));
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
        editor->setSize (375, 250);
        editor->addToDesktop (0);
        editor->setVisible (true);
        auto* spectrum = findSpectrum (*editor, findSpectrum);
        require (spectrum != nullptr && spectrum->averageChoice() == average::Choice::seconds3, "editor opens with the project average");
        auto* button = dynamic_cast<juce::Button*> (spectrum->findChildWithID ("spectrum-average"));
        require (button != nullptr && button->isShowing(), "AVG is a real visible button at 125 percent");
        require (button->getAccessibilityHandler() != nullptr
            && button->getAccessibilityHandler()->getRole() == juce::AccessibilityRole::button,
                 "AVG exposes its action to accessibility");
        require (! button->getMouseClickGrabsKeyboardFocus(), "mouse use leaves host keyboard focus alone");
        auto* peer = editor->getPeer();
        const auto tabToAverage = [&]
        {
            auto traverser = editor->createKeyboardFocusTraverser();
            const auto controls = traverser->getAllComponents (editor.get());
            require (! controls.empty(), "the editor has a Tab order");
            controls.front()->grabKeyboardFocus();
            // Always send a Tab, even if AVG was the first control.
            for (size_t index = 0; index <= controls.size(); ++index)
            {
                require (peer->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey)), "the peer dispatches Tab");
                if (button->hasKeyboardFocus (false)) return;
            }
            require (false, "Tab reaches AVG through the shipping editor");
        };
        for (const auto key : { juce::KeyPress::returnKey, juce::KeyPress::spaceKey })
        {
            processor.setSpectrumAveragePreference (average::Choice::seconds3);
            spectrum->setAverageChoice (average::Choice::seconds3);
            tabToAverage();
            require (peer->handleKeyPress (juce::KeyPress (key)), "Enter and Space activate AVG");
            require (hypha::tests::afterQueuedMessages ([&]
            {
                auto* menu = juce::Component::getCurrentlyModalComponent();
                require (menu != nullptr, "the real AVG menu opens from the keyboard");
                require (menu->keyPressed (juce::KeyPress (juce::KeyPress::upKey))
                    && menu->keyPressed (juce::KeyPress (key)), "keys select 10 S in the actual menu");
            }), "AVG activation is dispatched");
            require (hypha::tests::afterQueuedMessages ([] {}), "menu selection is delivered");
            require (processor.spectrumAverage.get() == average::Choice::seconds10, "keyboard selection updates the project");
            require (button->getButtonText() == "AVG 10 S" && button->getAccessibilityHandler()->getTitle() == "AVG 10 S",
                     "the accessible label follows the choice");
            processor.getStateInformation (bytes);
            require (juce::AudioProcessor::getXmlFromBinary (bytes.getData(), static_cast<int> (bytes.getSize()))
                         ->getStringAttribute ("freq_curve_average") == "10 S", "keyboard selection is saved");
        }
        tabToAverage();
        peer->handleKeyPress (juce::KeyPress (juce::KeyPress::returnKey));
        require (hypha::tests::afterQueuedMessages ([]
        {
            auto* menu = juce::Component::getCurrentlyModalComponent();
            require (menu != nullptr && menu->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)), "Escape dismisses AVG");
        }) && hypha::tests::afterQueuedMessages ([] {}), "menu cancellation is delivered");
        require (processor.spectrumAverage.get() == average::Choice::seconds10, "closing a menu changes nothing");
        const auto togglePsb = [&]
        {
            const auto bounds = spectrum->getLocalBounds().toFloat();
            const auto point = hypha::spectrum_geometry::subviewBoundsFor (
                hypha::spectrum_geometry::plotBoundsFor (bounds), hypha::spectrum_geometry::visualScaleFor (bounds)).getCentre();
            spectrum->mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), point, {}, 0, 0, 0, 0, 0,
                spectrum, spectrum, {}, point, {}, 0, false });
        };
        togglePsb();
        require (spectrum->isPsbObservation() && ! button->isVisible()
            && ! hypha::tests::editor_reachability::keyboardReaches (*button), "PSB has no AVG control or Tab stop");
        togglePsb();
        require (! spectrum->isPsbObservation() && button->isVisible() && button->getButtonText() == "AVG 10 S",
                 "returning from PSB restores the selected AVG action");
        editor->setSize (300, 200);
        require (! button->isVisible() && button->getBounds().isEmpty()
            && ! hypha::tests::editor_reachability::keyboardReaches (*button), "100 percent has no AVG target or Tab stop");
        require (processor.spectrumAverage.get() == average::Choice::seconds10, "shrinking the view retains AVG");
    }
    std::unique_ptr<juce::AudioProcessorEditor> reopened (processor.createEditorIfNeeded());
    require (findSpectrum (*reopened, findSpectrum)->averageChoice() == average::Choice::seconds10, "editor reopen retains the project average");
    reopened.reset();
    // Open the actual asynchronous callout in the shipping editor, then close it as a host does.
    using Control = hypha::reference_ui::AWindowControl;
    juce::Component otherModal;
    otherModal.setVisible (true);
    otherModal.enterModalState (false);
    const auto baselineModals = juce::Component::getNumCurrentlyModalComponents();
    for (const auto close : { 0, 1, 2, 3 })
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
        auto* control = component<Control> (*editor);
        require (control != nullptr, "the shipping editor owns the A window control");
        editor->addToDesktop (0);
        for (auto* parent = static_cast<juce::Component*> (control); parent != nullptr; parent = parent->getParentComponent())
            parent->setVisible (true);
        control->choose (control->currentTarget(), Control::secondsItem);
        juce::Component::SafePointer<juce::Component> popup (juce::Component::getCurrentlyModalComponent());
        require (popup != nullptr && dynamic_cast<juce::CallOutBox*> (popup.getComponent()) != nullptr
            && popup->isVisible() && juce::Component::getNumCurrentlyModalComponents() == baselineModals + 1,
                 "SECONDS opens a real modal callout");
        if (close == 0) editor->setVisible (false);
        else if (close == 1) control->getParentComponent()->setVisible (false);
        // VST3/AAX wrappers detach the native peer and then destroy their editor.
        else if (close == 2) { editor->removeFromDesktop(); editor.reset(); }
        else editor.reset();
        const bool closed = (popup == nullptr || (! popup->isCurrentlyModal() && ! popup->isVisible()))
            && juce::Component::getNumCurrentlyModalComponents() == baselineModals;
        if (! closed) std::cerr << "Popup close case " << close << ": modals="
            << juce::Component::getNumCurrentlyModalComponents() << " baseline=" << baselineModals << '\n';
        require (closed,
                 "hiding or destroying the editor leaves no input modal");
        require (otherModal.isCurrentlyModal (false), "editor closure leaves unrelated modals alone");
    }
    std::cout << "Project display windows: old state, per-role seconds, FREQ state and editor reopen PASS\n";
}
