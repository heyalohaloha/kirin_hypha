#pragma once
// Included inside the editor product fixture namespace; uses its pointer and pixel helpers.
juce::Image stableRecordEditorImage (KirinHyphaEditor& editor)
{
    // MaterialCache deliberately paints a new key directly, then rasterises it on reuse.
    // Compare Record transitions after that documented cold/warm boundary, with zero tolerance.
    editor.createComponentSnapshot (editor.getLocalBounds());
    editor.createComponentSnapshot (editor.getLocalBounds());
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    require (differentPixels (image, editor.createComponentSnapshot (editor.getLocalBounds()),
                              image.getBounds()) == 0, "warmed editor render is reproducible");
    return image;
}

juce::Image stableRecordCaptureImage (KirinHyphaEditor& editor, juce::Rectangle<int> body)
{
    (editor.*testMember (FreezeCapture {})) (1'200, 800);
    (editor.*testMember (FreezeCapture {})) (1'200, 800);
    const auto image = (editor.*testMember (FreezeCapture {})) (1'200, 800).image;
    require (differentPixels (image, (editor.*testMember (FreezeCapture {})) (1'200, 800).image,
                              body) == 0, "warmed immutable Capture body is reproducible");
    return image;
}

void verifyRecordBodyOwnership()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    processor.setMeterContextPreference (hypha::meter_context::MeterContext::trackStem, false);
    processor.setObservatoryDomainPreference (hypha::observatory::stateValue (Domain::frequency));
    processor.prepareToPlay (48'000, 960);
    auto editor = std::unique_ptr<KirinHyphaEditor> (
        dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
    require (editor != nullptr, "Record presentation shipping editor opens");
    editor->setSize (600, 400); editor->setVisible (true);
    auto* view = component<hypha::observatory::View> (*editor);
    auto* spectrum = component<hypha::SpectrumComponent> (*editor);
    auto* absolute = component<hypha::AbsoluteComponent> (*editor);
    auto* attack = component<hypha::AttackComponent> (*editor);
    require (view && spectrum && absolute && attack, "Record uses the shipping component tree");
    struct Case { Domain domain; AnalysisPage page; juce::Component* expected; const char* name; };
    const std::array<Case, 6> cases {{
        { Domain::frequency, AnalysisPage::spectrum, spectrum, "FREQ" },
        { Domain::time, AnalysisPage::perceptual, absolute, "SHARP" },
        { Domain::time, AnalysisPage::absolute, absolute, "LIVE" },
        { Domain::time, AnalysisPage::attack, attack, "ATTACK" },
        { Domain::space, AnalysisPage::meters, nullptr, "SPACE" },
        { Domain::level, AnalysisPage::meters, nullptr, "LEVEL" },
    }};
    for (const auto& test : cases)
    {
        (editor.get()->*testMember (SetObservatoryDomain {})) (test.domain);
        if (test.expected)
        {
            (editor.get()->*testMember (SetAnalysisPage {})) (test.page);
            require (test.expected->isVisible(), "selected analysis owns its body");
        }
        const auto domainBefore = view->domain();
        const auto editorBody = editor->getLocalArea (view, view->analysisBodyBounds());
        const auto footer = editor->getLocalArea (view, view->statusStripBounds());
        const auto captureBody = view->captureBodyBounds (1'200, 800, false);
        stableRecordCaptureImage (*editor, captureBody);
        const auto baseline = stableRecordEditorImage (*editor);
        const bool inspectResult = test.domain == Domain::level;
        for (const auto phase : { KIRIN_RECORD_DISPLAY_FINALIZING,
                                 KIRIN_RECORD_DISPLAY_RESULT_HOLD, KIRIN_RECORD_DISPLAY_UNAVAILABLE })
        {
            KirinRecordDisplay record {};
            record.phase = static_cast<std::uint8_t> (phase); record.generation = 42;
            record.has_measure = 1; record.has_session = 1;
            record.measure.lufs_m = -17.2; record.measure.lufs_s = -16.8;
            record.measure.crest = 11.1; record.measure.psr = 9.4;
            record.measure.sharpness = 1.3; record.session.max_true_peak = -0.8;
            record.session.lufs_i = -16.1;
            view->setRecordDisplay (record, true);
            require (view->recordBodyActive() == inspectResult, "only LEVEL inspects Record result facts");
            require (! test.expected || test.expected->isVisible(), "Record preserves the selected analysis sibling");
            const auto editorFirst = stableRecordEditorImage (*editor);
            const auto captureFirst = stableRecordCaptureImage (*editor, captureBody);
            if (! inspectResult)
            {
                int outsideFooter = 0;
                juce::Rectangle<int> changed;
                for (int y = 0; y < baseline.getHeight(); ++y)
                    for (int x = 0; x < baseline.getWidth(); ++x)
                        if (! footer.contains (x, y) && baseline.getPixelAt (x, y) != editorFirst.getPixelAt (x, y))
                            { ++outsideFooter; changed = changed.getUnion ({ x, y, 1, 1 }); }
                if (outsideFooter != 0)
                {
                    std::cerr << test.name << " phase=" << phase << " changed=" << outsideFooter
                              << " bounds=" << changed.toString() << " footer=" << footer.toString() << '\n';
                    const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_EDITOR_RECORD_DEBUG", {});
                    if (output.isNotEmpty())
                        for (const auto& pair : { std::make_pair ("before", baseline), std::make_pair ("after", editorFirst) })
                        {
                            auto stream = juce::File (output).getChildFile (juce::String (test.name) + "-" + pair.first + ".png").createOutputStream();
                            require (stream != nullptr && juce::PNGImageFormat().writeImageToStream (pair.second, *stream), "debug image");
                        }
                }
                require (outsideFooter == 0, "Keep does not overlay selected body, tabs or VU calibration");
            }
            record.measure.lufs_m = -37.2; record.measure.lufs_s = -36.8;
            record.session.lufs_i = -36.1;
            view->setRecordDisplay (record, true);
            const auto editorSecond = stableRecordEditorImage (*editor);
            const auto captureSecond = stableRecordCaptureImage (*editor, captureBody);
            const auto editorChanges = differentPixels (editorFirst, editorSecond, editorBody);
            const auto captureChanges = differentPixels (captureFirst, captureSecond, captureBody);
            if (inspectResult && phase == KIRIN_RECORD_DISPLAY_RESULT_HOLD)
            {
                require (editorChanges > 100, "LEVEL result values reach the shipping body");
                require (captureChanges > 100, "LEVEL result values reach immutable Capture v1");
            }
            else if (! inspectResult)
            {
                require (editorChanges == 0, "held Record values do not replace selected analysis facts");
                require (captureChanges == 0, "Capture v1 retains the same selected analysis presentation");
            }
            record.phase = KIRIN_RECORD_DISPLAY_WATCH;
            view->setRecordDisplay (record, false);
            require (! view->recordBodyActive(), "Watch releases result inspection");
            require (! test.expected || test.expected->isVisible(), "selected analysis remains visible after Record");
            require (view->domain() == domainBefore, "Record preserves the selected domain");
        }
        std::cout << "Record presentation " << test.name << ": PASS" << std::endl;
    }
    processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
}
