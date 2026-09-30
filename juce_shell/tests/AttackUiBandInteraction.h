#pragma once

// Pointer, hover, LIVE and language contracts of the DRUM band view, and its paint budget.
// Included by AttackUiBandContract.h.
namespace hypha::attack_ui_test
{
inline juce::MouseEvent bandMouse (juce::Component& component, juce::Point<float> at)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f,
             0.0f, &component, &component, now, at, now, 0, false };
}

// Chips choose; while LIVE the panes and the reading do not select and nothing is fetched; a dot
// locks its hit (its envelope fetched), the same dot or an empty place on the line returns, as END
// does; the hover help names what the pointer is on, LIVE or locked; a PRE that predates bands is
// explained.
inline bool verifyBandInteraction()
{
    auto scene = presetScene (observatory::sizePresets.back());
    auto& component = *scene.component;
    const auto& layout = scene.layout;
    const auto context = presentation::forEditor (900, 600);
    auto fixture = summaryFixture (3);
    fixture.lanes.submit (component);
    int chosen = -1;
    component.onBandChange = [&chosen] (std::uint8_t band) { chosen = band; };
    const auto at = [&component] (juce::Point<float> point) { return bandMouse (component, point); };
    const auto click = [&component, &at] (juce::Rectangle<int> area) { component.mouseDown (at (area.getCentre().toFloat())); };
    const auto hover = [&component, &at] (juce::Rectangle<int> area) { component.mouseMove (at (area.getCentre().toFloat())); };
    const auto tip = [&component] (const juce::String& expected, const char* where) {
        if (component.getTooltip() == expected)
            return true;
        std::cerr << "hover help over " << where << ": " << component.getTooltip() << '\n';
        return false; };
    click (rectangle (attack_band::chipCell (layout, context, 3)));
    if (component.band() != 3 || chosen != 3)
        return false;
    EnvelopeSource source { fixture.batch.get() };
    showSummary (component, fixture);
    component.bandEnvelopeSource = source;
    component.setBandSnapshot (*fixture.batch);
    const auto live = renderAttack (component);
    if (! source.asked->empty())
    {
        std::cerr << "a hit's envelope was fetched while LIVE shows the summary\n";
        return false;
    }
    for (const auto area : { rectangle (attack_band::headPane (layout)), readingArea (layout) })
    {
        click (area);
        if (differences (live, renderAttack (component)) != 0)
        {
            std::cerr << "a click on the panes or the reading changed the view\n";
            return false;
        }
    }
    hover (rectangle (attack_band::headPane (layout)));
    if (! tip (attack_band_painter::paneTooltip (true), "HEAD")) return false;
    hover (rectangle (attack_band::tailPane (layout)));
    if (! tip (attack_band_painter::paneTooltip (false), "TAIL")) return false;
    hover (readingArea (layout));
    if (! tip (attack_band_summary_painter::cardTooltip(), "the card")) return false;
    hover (rectangle (layout.lanes[0]));
    if (! tip (attack_band_summary_painter::laneTooltip (0, true), "a number line")) return false;
    // A dot locks its hit: LEVEL's sixth summed hit.
    const auto plot = rectangle (attack_ui::lanePlot (layout, 3));
    const auto dot = attack_band_summary_painter::dotCentre (fixture.summary, 3, plot, 5);
    component.mouseDown (at (dot));
    const auto key = fixture.summary.event_samples[5];
    if (differences (live, renderAttack (component)) == 0 || source.asked->empty() || source.asked->back() != key)
    {
        std::cerr << "a dot did not lock its hit, or the hit's envelope was not fetched\n";
        return false;
    }
    hover (rectangle (layout.lanes[2]));
    if (! tip (attack_band_painter::laneTooltip (attack_lanes::Lane::release), "a locked hit's lane")) return false;
    const auto returned = [&] (const char* how) {
        if (differences (live, renderAttack (component)) == 0)
            return true;
        std::cerr << how << " did not return to the summary\n";
        return false; };
    component.mouseDown (at (dot));
    if (! returned ("the locked dot")) return false;
    component.mouseDown (at (dot));
    component.mouseDown (at ({ static_cast<float> (plot.getX() + 14), static_cast<float> (plot.getCentreY()) }));
    if (! returned ("an empty place on the line")) return false;
    component.mouseDown (at (dot));
    click (rectangle (attack_ui::readoutCell (layout, layout.axis)));
    if (! returned ("NOW beside the scale")) return false;
    component.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    component.keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    if (! returned ("END")) return false;
    hover (rectangle (attack_band::chipCell (layout, context, 5)));
    if (! tip (attack_band_painter::chipTooltip (5), "a chip")) return false;
    component.mouseExit (bandMouse (component, {}));
    if (! tip ({}, "nothing")) return false;
    // A PRE that predates bands: the chosen chip says so and what to do.
    auto older = bandBatchFor (fixture.lanes, 3, KIRIN_ATTACK_BAND_PRE_PREDATES);
    component.setBandSnapshot (*older);
    if (component.preBand() != attack_band::PreBand::predates)
        return false;
    hover (rectangle (attack_band::chipCell (layout, context, 3)));
    if (! tip (attack_band_painter::predatesTooltip(), "the chosen chip with an older PRE")) return false;
    click (rectangle (attack_band::chipCell (layout, context, 0)));
    if (component.band() != 0 || chosen != 0)
        return false;
    hover (rectangle (layout.lanes[0]));
    if (! tip ({}, "the whole-signal lanes"))
        return false;
    // 125%: the small number lines in HISTORY lock a hit as the lanes do, and name their lane.
    {
        auto line = presetScene (observatory::sizePresets[1]);
        showSummary (*line.component, fixture);
        const auto rows = attack_band_summary_painter::rowPlots (rectangle (line.layout.history),
                                                                 presentation::forEditor (450, 300));
        const auto summaryImage = renderAttack (*line.component);
        line.component->mouseMove (bandMouse (*line.component, rows[1].getCentre().toFloat()));
        if (rows[3].isEmpty() || line.component->getTooltip() != attack_band_summary_painter::laneTooltip (1, true))
        {
            std::cerr << "the 125% number lines are missing or unnamed\n";
            return false;
        }
        line.component->mouseDown (bandMouse (*line.component,
                                              attack_band_summary_painter::dotCentre (fixture.summary, 3, rows[3], 5)));
        if (differences (summaryImage, renderAttack (*line.component)) == 0)
        {
            std::cerr << "a dot at 125% did not lock its hit\n";
            return false;
        }
    }
    // 100% has no chips: a click where they would stand leaves the band alone.
    auto compact = presetScene (observatory::sizePresets.front());
    fixture.lanes.submit (*compact.component);
    compact.component->setBand (2);
    chosen = -1;
    compact.component->onBandChange = [&chosen] (std::uint8_t band) { chosen = band; };
    compact.component->mouseDown (bandMouse (*compact.component, { 40.0f, 8.0f }));
    return compact.component->band() == 2 && chosen == -1;
}

// Every piece of band prose the view can show has Japanese, checked on the texts as the view
// builds them (a pattern with %1 to %3 is matched at run time, not only in the source), and the
// band view paints in Japanese at every size.
inline bool verifyBandTranslations()
{
    using namespace attack_band_painter;
    std::vector<juce::String> prose { predatesTooltip(), paneTooltip (true), paneTooltip (false),
                                      playText (0), "MEASURING", "NO HIT", "NO SOUND", "NO PAIR" };
    for (std::uint8_t band = 0; band <= attack_band::bandCount; ++band)
    {
        prose.push_back (chipTooltip (band));
        if (band != 0)
            prose.push_back (playText (band));
    }
    for (const auto lane : attack_lanes::bandLanes)
        prose.push_back (laneTooltip (lane));
    attack_lanes::Hit hit {};
    hit.pre.available = hit.post.available = true;
    for (const auto reason : { attack_lanes::Reason::ringing, attack_lanes::Reason::noSound,
                               attack_lanes::Reason::preNoSound, attack_lanes::Reason::postNoSound,
                               attack_lanes::Reason::longTail, attack_lanes::Reason::notMeasured,
                               attack_lanes::Reason::updatePre, attack_lanes::Reason::noPair,
                               attack_lanes::Reason::nextHit, attack_lanes::Reason::quietBody })
    {
        // The full reason and the short word the narrowest cells show in its place.
        prose.push_back (attack_lane_painter::reasonText (hit, reason));
        if (const auto brief = attack_lane_painter::shortReasonText (hit, reason); brief != "--")
            prose.push_back (brief);
    }
    // The summary's words: directions, readings, cards, withheld lanes, axes and hover help.
    namespace summary = attack_band_summary;
    auto kicks = summaryFixture (1);
    auto hidden = summaryFixture (1);
    auto cut = summaryFixture (1);
    auto past = summaryFixture (1);
    for (std::uint32_t item = 0; item < kicks.batch->count; ++item)
    {
        hidden.batch->hits[item].pre.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING;
        cut.batch->hits[item].post.release_state = KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT;
        past.batch->hits[item].post.release_state = KIRIN_ATTACK_BAND_RELEASE_AT_LEAST;
    }
    for (auto* fixture : { &hidden, &cut, &past })
        fixture->resum();
    const auto name = nameText (1);
    const auto title = summary::titleText (name, kicks.summary.count);
    prose.insert (prose.end(), { summary::leftOutText (name, 1), summary::leftOutText (name, 3), title,
                                 summary::titleText (name, 1), summary::lastText (8), summary::lastText (1), "SAME",
                                 "- SMALLER / EARLIER", "LATER / LARGER +", "- SMALLER", "LARGER +", "0 = SAME",
                                 "POST VALUES", attack_band_summary_painter::cardTooltip() });
    for (std::size_t lane = 0; lane < summary::laneCount; ++lane)
    {
        prose.insert (prose.end(), { summary::directionWord (lane, 1.0f), summary::directionWord (lane, -1.0f),
                                     attack_band_summary_painter::laneTooltip (lane, true),
                                     attack_band_summary_painter::laneTooltip (lane, false) });
        for (const auto* fixture : { &kicks, &hidden, &cut, &past })
            if (const auto word = summary::wordText (fixture->summary, lane); word.isNotEmpty())
                prose.push_back (word);
        for (const auto* fixture : { &kicks, &hidden, &cut, &past })
            if (const auto why = summary::withheldText (fixture->summary, lane, "NO PAIR"); why != "--")
                prose.push_back (why);
    }
    for (const auto* fixture : { &kicks, &hidden, &cut, &past })
        for (const auto& fact : summary::cardFacts (fixture->summary, "NO PAIR"))
        {
            // A moved lane's bare form is its value (a number and a unit); a quiet one's is a word.
            prose.insert (prose.end(), { fact.text, fact.brief });
            if (fact.quiet)
                prose.push_back (fact.bare);
        }
    // POST's own values are labels and units (English); what says why DELAY has none is prose.
    for (const auto& reason : { juce::String ("NO PAIR"), juce::String ("UPDATE PRE") })
        for (const auto& fact : summary::cardFacts (summaryFixture (1, KIRIN_ATTACK_BAND_PRE_PREDATES).summary, reason))
            if (fact.quiet)
                prose.push_back (fact.text);
    for (const auto& text : prose)
        if (! i18n::hasTranslation (text))
        {
            std::cerr << "no Japanese for \"" << text << "\"\n";
            return false;
        }
    const i18n::ScopedLanguage japanese (i18n::Language::japanese);
    for (const auto& preset : observatory::sizePresets)
    {
        auto scene = presetScene (preset);
        showSummary (*scene.component, kicks);
        const auto live = renderAttack (*scene.component);
        scene.component->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        const auto locked = renderAttack (*scene.component);
        if (live.getWidth() != scene.component->getWidth()
            || ! writeBandPreview (preset.label, "_ja_summary", live)
            || ! writeBandPreview (preset.label, "_ja_locked", locked))
            return false;
    }
    return true;
}

// A locked hit at the narrow sizes says why a value is withheld instead of "--": the 125% readout,
// and the 100% glance, where a reason is drawn in its own face and never changes how the other
// values read. The LIVE summary's reasons: verifySummaryReasons.
inline bool verifyBandReasonsAtSmallSizes()
{
    {
        auto scene = presetScene (observatory::sizePresets[1]);
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        fixture.submit (*scene.component);
        scene.component->setBand (4);
        auto ringing = bandBatchFor (fixture, 4);
        auto pending = bandBatchFor (fixture, 4);
        for (std::uint32_t item = 0; item < ringing->count; ++item)
        {
            ringing->hits[item].pre.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING;
            pending->hits[item].pre = sideIn (KIRIN_ATTACK_BAND_SIDE_PENDING);
        }
        scene.component->setBandSnapshot (*ringing);
        scene.component->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
        scene.component->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        const auto stated = renderAttack (*scene.component);
        scene.component->setBandSnapshot (*pending);
        if (differences (stated, renderAttack (*scene.component),
                         rectangle (attack_ui::lineCell (scene.layout, 0))) == 0)
        {
            std::cerr << "the 125% readout hides why DELAY is withheld\n";
            return false;
        }
    }
    // An older PRE (DELAY: UPDATE PRE) and no pair (DELAY: NO PAIR) leave the same three POST
    // values in the same face at 100%; only DELAY reads differently.
    const auto glanceWith = [] (bool paired) {
        auto scene = presetScene (observatory::sizePresets.front());
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        if (! paired)
            fixture.pairs->status = KIRIN_SPECTRUM_NO_PAIR;
        fixture.submit (*scene.component);
        scene.component->setBand (4);
        scene.component->setBandSnapshot (*bandBatchFor (fixture, 4, KIRIN_ATTACK_BAND_PRE_PREDATES));
        scene.component->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        return std::pair { renderAttack (*scene.component), scene.layout };
    };
    const auto [older, layout] = glanceWith (true);
    const auto alone = glanceWith (false).first;
    for (std::size_t lane = 1; lane < attack_ui::laneCount; ++lane)
        if (differences (older, alone, rectangle (attack_ui::lineCell (layout, lane))) != 0)
        {
            std::cerr << "a withheld DELAY changed the 100% glance's other values\n";
            return false;
        }
    return differences (older, alone, rectangle (attack_ui::lineCell (layout, 0))) != 0;
}

// Run with KIRIN_ATTACK_FRAME_BUDGET set: the 300% band view with 60 hits and their summary,
// changing every frame, against the same ceilings as the whole-signal view.
inline bool verifyBandFrameBudget()
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_FRAME_BUDGET", {}).isEmpty())
        return true;
    material_cache::Lifetime materialCache;
    bool withinBudget = true;
    auto fixture = laneFixture ({ 4'800, 9'600, 14'400, 19'200, 24'000, 28'800, 33'600, 38'400, 43'200,
        48'000, 52'800, 57'600, 62'400, 67'200, 72'000, 76'800, 81'600, 86'400, 91'200, 96'000,
        100'800, 105'600, 110'400, 115'200, 120'000, 124'800, 129'600, 134'400, 139'200, 144'000,
        148'800, 153'600, 158'400, 163'200, 168'000, 172'800, 177'600, 182'400, 187'200, 192'000,
        196'800, 201'600, 206'400, 211'200, 216'000, 220'800, 225'600, 230'400, 235'200, 240'000,
        244'800, 249'600, 254'400, 259'200, 264'000, 268'800, 273'600, 278'400, 283'200, 288'000 });
    auto batch = bandBatchFor (fixture, 4);
    for (const auto dpi : { 1.0f, 2.0f })
    for (const bool overlay : { true, false })
    {
        auto scene = presetScene (observatory::sizePresets.back());
        auto& component = *scene.component;
        component.setOverlayMode (overlay);
        component.bandEnvelopeSource = EnvelopeSource { batch.get() };
        component.setBand (4);
        juce::Image image (juce::Image::ARGB, static_cast<int> (std::ceil (component.getWidth() * dpi)),
                           static_cast<int> (std::ceil (component.getHeight() * dpi)), true);
        std::array<double, 6> samples {};
        for (std::size_t frame = 0; frame < samples.size(); ++frame)
        {
            for (std::uint32_t hit = 0; hit < batch->count; ++hit)
                batch->hits[hit].post.level_dbfs = -6.8f - 0.1f * static_cast<float> (frame);
            const auto summary = summaryFor (*batch);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            fixture.submit (component, 288'000 + static_cast<std::int64_t> (frame) * 480);
            component.setBandSnapshot (*batch);
            component.setBandSummary (summary);
            component.presentationTickAt (start + 101.0);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (dpi));
            component.paintEntireComponent (g, true);
            samples[frame] = juce::Time::getMillisecondCounterHiRes() - start;
        }
        std::sort (samples.begin() + 1, samples.end());
        std::cout << "DRUM band frame: dpi=" << dpi << " overlay=" << overlay << " cold_ms=" << samples[0]
                  << " median_ms=" << samples[3] << " max_ms=" << samples[5] << '\n';
#if ! JUCE_DEBUG
        withinBudget = withinBudget && samples[3] <= 12.0 && samples[5] <= 24.0 && samples[0] <= 80.0;
#endif
    }
    return withinBudget;
}
}
