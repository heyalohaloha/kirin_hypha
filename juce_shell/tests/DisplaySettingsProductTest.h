#pragma once

// Included after the shipping processor/editor fixture helpers.
struct DisplayMenuAccess
{
    using Type = juce::PopupMenu (KirinHyphaEditor::*)();
    friend Type testMember (DisplayMenuAccess);
};
template struct TestAccess<DisplayMenuAccess, &KirinHyphaEditor::operationsMenu>;
struct DisplayChoiceAccess
{
    using Type = void (KirinHyphaEditor::*) (int);
    friend Type testMember (DisplayChoiceAccess);
};
template struct TestAccess<DisplayChoiceAccess, &KirinHyphaEditor::handleOperationsMenu>;

inline void verifyDisplaySettingsProduct()
{
    namespace settings = hypha::display_settings;
    for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        Processor processor (role);
        const auto save = [&]
        {
            juce::MemoryBlock bytes; processor.getStateInformation (bytes);
            return juce::AudioProcessor::getXmlFromBinary (bytes.getData(), static_cast<int> (bytes.getSize()));
        };
        for (int field = 0; field < settings::fieldCount; ++field)
        {
            require (! save()->hasAttribute (settings::keys[(size_t) field]), "display defaults are absent from state");
            for (int choice = 0; settings::valid (field, choice); ++choice)
            {
                settings::Values values; values.selected[(size_t) field] = choice;
                processor.setDisplaySettingsPreference (values);
                juce::MemoryBlock bytes; processor.getStateInformation (bytes);
                processor.setDisplaySettingsPreference ({});
                processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
                require (processor.displaySettings.get() == values, "shipping state round trips every display choice");
            }
            auto xml = save(); xml->setAttribute (settings::keys[(size_t) field], "unknown");
            juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary (*xml, bytes);
            processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            require (processor.displaySettings.get().selected[(size_t) field] == 0, "unknown display values restore defaults");
            processor.setDisplaySettingsPreference ({});
        }
        juce::XmlElement legacy ("KirinHyphaState");
        juce::MemoryBlock bytes; juce::AudioProcessor::copyXmlToBinary (legacy, bytes);
        settings::Values values; values.selected.fill (1); processor.setDisplaySettingsPreference (values);
        processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        require (processor.displaySettings.get() == settings::Values {}, "old state keeps the established display");
    }
    Processor processor (Processor::Role::Post);
    processor.setObservatoryDomainPreference (hypha::observatory::stateValue (Domain::frequency));
    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_DISPLAY_PREVIEWS", {});
    for (const auto language : { hypha::i18n::Language::english, hypha::i18n::Language::japanese })
    {
        const hypha::i18n::ScopedLanguage scoped (language);
        processor.setObservatoryDomainPreference (hypha::observatory::stateValue (Domain::frequency));
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());
        auto& shipping = *static_cast<KirinHyphaEditor*> (editor.get());
        editor->addToDesktop (0); editor->setVisible (true);
        auto* spectrum = component<hypha::SpectrumComponent> (*editor);
        require (spectrum != nullptr, "the shipping editor owns FREQ");
        spectrum->setSignalActive (true);
        require (spectrum->frameDriverActiveForTest(), "a showing FREQ owns its VBlank attachment");
        editor->setVisible (false);
        require (! spectrum->frameDriverActiveForTest(), "hiding an ancestor releases the FREQ frame driver");
        editor->setVisible (true);
        require (spectrum->frameDriverActiveForTest(), "showing the editor resumes frame presentation");
        auto* vu = dynamic_cast<juce::Button*> (find (*editor, "observatory-hybrid-vu"));
        require (vu != nullptr, "the shipping VU control is available");
        vu->onClick(); require (! spectrum->frameDriverActiveForTest(), "VU stops the covered FREQ animator");
        vu->onClick(); require (spectrum->frameDriverActiveForTest(), "returning from VU resumes the same FREQ selection");
        for (int field = 0; field < settings::fieldCount; ++field)
            for (int choice = 0; settings::valid (field, choice); ++choice)
            {
                const int action = settings::firstAction + field * 10 + choice;
                const auto menu = (shipping.*testMember (DisplayMenuAccess {}))();
                bool found = false;
                for (juce::PopupMenu::MenuItemIterator item (menu, true); item.next();)
                    if (item.getItem().itemID == action)
                    { found = true; require (item.getItem().text == settings::labels[(size_t) field][(size_t) choice], "menu uses the short choice label"); }
                require (found, "each choice is reachable through the shipping menu");
                (shipping.*testMember (DisplayChoiceAccess {})) (action);
                require (processor.displaySettings.get().selected[(size_t) field] == choice, "menu handler writes the project preference");
                if (field != settings::rate) require (spectrum->displaySettingsForTest().selected[(size_t) field] == choice,
                                                     "the actual FREQ view receives each menu change");
            }
        if (preview.isNotEmpty())
        {
            const auto menu = (shipping.*testMember (DisplayMenuAccess {}))();
            for (juce::PopupMenu::MenuItemIterator item (menu, true); item.next();)
            {
                const auto& entry = item.getItem();
                if (! entry.subMenu || (entry.text != "RANGE" && entry.text != "TILT" && entry.text != "SMOOTH"
                    && entry.text != "LIGHT" && entry.text != "PEAK" && entry.text != "LEVEL / NUM RATE")) continue;
                auto choices = *entry.subMenu;
                choices.setLookAndFeel (&editor->getLookAndFeel());
                choices.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (editor.get()).withMinimumWidth (190), [] (int) {});
                {
                    auto* popup = juce::Component::getCurrentlyModalComponent();
                    require (popup != nullptr, "the native display choice popup opens");
                    const auto image = popup->createComponentSnapshot (popup->getLocalBounds());
                    const auto directory = juce::File (preview); require (directory.createDirectory().wasOk(), "menu preview directory");
                    const auto name = entry.text == "LEVEL / NUM RATE" ? juce::String ("NUM-RATE") : entry.text;
                    const auto file = directory.getChildFile ("menu-" + name
                        + (language == hypha::i18n::Language::english ? "-en.png" : "-ja.png"));
                    file.deleteFile(); const auto output = file.createOutputStream();
                    require (output && juce::PNGImageFormat().writeImageToStream (image, *output), "native menu preview written");
                    popup->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                }
                require (hypha::tests::afterQueuedMessages ([] {}), "menu preview closes without changing a choice");
            }
        }
        for (int width : { 300, 375, 450, 600, 900 })
            for (const auto domain : { Domain::frequency, Domain::level })
            {
                (shipping.*testMember (SetObservatoryDomain {})) (domain);
                editor->setSize (width, width * 2 / 3);
                if (domain == Domain::frequency)
                {
                    spectrum->setAbsoluteObservation (true); spectrum->setLowScope (true);
                    spectrum->setSignalActive (true); spectrum->setSnapshot (hypha::tests::lowFrame (48'000, false));
                }
                auto* view = component<hypha::observatory::View> (*editor);
                if (domain == Domain::frequency)
                    require (! view->getLocalArea (spectrum, spectrum->getLocalBounds()).intersects (view->statusStripBounds()),
                             "FREQ axes stay clear of the folded status strip through the shipping layout");
                KirinObservatoryFrame frame {}; frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
                frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE; frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
                frame.meter.sample_rate = 48000; frame.meter.channels = 2;
                frame.meter.lufs_m = -16.2; frame.meter.lufs_s = -15.8; frame.meter.lufs_i = -17.1;
                frame.meter.true_peak = -1.2; frame.meter.max_true_peak = -0.4; frame.meter.plr = 15.0;
                KirinWatchDisplay watch {}; watch.current.lufs_s = -15.8; watch.current.crest = 14;
                watch.current.psr = 13; watch.maximum = watch.current; view->setWatchDisplay (watch, true);
                view->setObservatoryFrame (frame, true);
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
                if (preview.isNotEmpty())
                {
                    const juce::File directory (preview); require (directory.createDirectory().wasOk(), "display preview directory");
                    const auto file = directory.getChildFile (juce::String (domain == Domain::frequency ? "freq-" : "level-")
                        + juce::String (width) + (language == hypha::i18n::Language::english ? "-en.png" : "-ja.png"));
                    file.deleteFile(); const auto output = file.createOutputStream();
                    require (output && juce::PNGImageFormat().writeImageToStream (image, *output), "display preview written");
                }
            }
    }
    for (int width : { 300, 375, 450, 600, 900 })
    {
        hypha::observatory::View view (hypha::observatory::Role::post); view.setSize (width, width * 2 / 3);
        view.setLevelNumericInterval (1000);
        KirinObservatoryFrame first {}; first.version = KIRIN_OBSERVATORY_FRAME_VERSION;
        first.signal_state = KIRIN_SIGNAL_STATE_ACTIVE; first.meter.state = KIRIN_METER_SESSION_ACTIVE;
        first.meter.channels = 2; first.meter.sample_rate = 48000; first.meter.lufs_m = -16.2; first.meter.lufs_s = -15.8;
        KirinWatchDisplay watch {}; watch.current.lufs_s = -15.8;
        view.setObservatoryFrame (first, true);
        const auto paintNumbers = [&] { return view.createComponentSnapshot (view.bodyBounds()); };
        const auto missing = paintNumbers();
        view.setWatchDisplay (watch, true);
        const auto firstHeld = paintNumbers();
        view.setLevelNumericInterval (0);
        const auto firstLive = paintNumbers();
        require (differentPixels (firstHeld, firstLive, firstHeld.getBounds()) == 0
                     && differentPixels (missing, firstHeld, firstHeld.getBounds()) > 20,
                 "first valid Watch data paints the same measured value as LIVE after an unavailable paint");
        view.setLevelNumericInterval (1000);
        paintNumbers();
        // Keep background energy and moving bars constant while testing a numerical change.
        auto next = first; next.meter.lufs_s = -8.6; next.meter.observed_frames = 4800;
        watch.current.lufs_s = -8.6; view.setWatchDisplay (watch, true);
        view.setObservatoryFrame (next, true);
        const auto held = paintNumbers();
        std::cout << "LEVEL numerical hold " << width << ": " << differentPixels (firstHeld, held, held.getBounds()) << " changed pixels\n";
        require (differentPixels (firstHeld, held, held.getBounds()) == 0,
                 "LEVEL's actual painter holds the numerical reading");
        require (view.frameForPoll().meter.lufs_s == -8.6 && view.frameForPoll().meter.observed_frames == 4800,
                 "LEVEL's published frame remains raw while the number is held");
        next.meter.generation++; view.setObservatoryFrame (next, true);
        const auto boundary = paintNumbers();
        view.setLevelNumericInterval (0);
        const auto live = paintNumbers();
        require (differentPixels (boundary, live, live.getBounds()) == 0
                     && differentPixels (held, boundary, held.getBounds()) > 20,
                 "a new session paints its current reading immediately through the real painter");
        for (const bool available : { false, true, false })
        {
            view.setLevelNumericInterval (1000); paintNumbers();
            next.meter.lufs_m = next.meter.lufs_s = next.meter.max_true_peak = available ? -11.3 : NAN;
            watch.current.lufs_s = watch.current.crest = available ? 12.4 : NAN;
            view.setWatchDisplay (watch, true); view.setObservatoryFrame (next, true);
            const auto changedAvailability = paintNumbers();
            view.setLevelNumericInterval (0);
            const auto latestAvailability = paintNumbers();
            require (differentPixels (changedAvailability, latestAvailability, latestAvailability.getBounds()) == 0,
                     "real LEVEL paints NaN/finite boundaries immediately with the same session and Active state");
        }
    }
    std::unique_ptr<juce::AudioProcessorEditor> reopened (processor.createEditorIfNeeded());
    require (component<hypha::SpectrumComponent> (*reopened)->displaySettingsForTest() == processor.displaySettings.get(),
             "reopening the shipping editor retains project display settings");
    std::cout << "Display settings product: menu, state, restore and full editors in both languages PASS\n";
}
