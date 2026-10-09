#include "AttackV2PresentationContractTest.h"
#include "AttackV2Fixtures.h"
#include "AttackUiLaneContract.h"
#include "../src/HyphaAttackV2Painter.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"
#include "AttackV2MaterialContract.h"
#include "AttackV2MainSurfaceContract.h"
#include "../src/HyphaAttackV2Painter.h"
#include <iostream>
#include <limits>

namespace hypha::attack_ui_test
{
namespace
{
using namespace attack_v2_test;
using namespace attack_v2;
#define Q(e) do { if (! (e)) { std::cerr << "DRUM V2 contract line " << __LINE__ << ": " << #e << '\n'; return false; } } while (false)
struct Rig
{
    std::unique_ptr<AttackComponent> view = std::make_unique<AttackComponent>();
    KirinAttackSingleV2Request last {};
    std::vector<KirinAttackSingleV2Request> requested;
    std::uint64_t nextToken = 1;
    bool busy = false;
    Rig()
    {
        view->setSize (580, 248); view->setPresentationContext (presentation::forEditor (600, 400));
        view->singleRequestSource = [this] (const auto& r, auto& token) {
            requested.push_back (r); last = r; if (busy) return KIRIN_SNAPSHOT_BUSY;
            token = nextToken++; return KIRIN_SNAPSHOT_SUCCESS; };
    }
    bool nav (std::vector<KirinSnapshotEventKey> keys, double now = 0, KirinSnapshotHeader h = header(), bool realtime = true)
    {
        auto wave = std::make_unique<KirinAttackWaveformBatch>();
        return view->setNavigationV2 (h, keys, *wave, *wave, now, realtime);
    }
    void click (juce::Point<int> p) { view->mouseDown (mouse (*view, p.toFloat())); }
};
bool completedLockSurvivesLiveWindow()
{
    Rig r; r.view->beginSnapshotV2();
    Q (r.nav ({ key (400000, 1) }));
    auto& state = r.view->presentationSnapshotV2();
    r.view->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    auto single = std::make_unique<KirinAttackSingleSnapshotV2> (attack_v2_test::single (r.last, r.view->singleRequestToken()));
    single->event = r.last.event; single->request_token = r.view->singleRequestToken();
    single->header.band = 0; single->header.target = KIRIN_TARGET_DELTA;
    single->finish = KIRIN_FINISH_FULL;
    Q (r.view->setSingleSnapshotV2 (*single, 100));
    const auto held = state.single;
    Q (held && !state.live);
    auto nav = header(); nav.cutoff_sample += 48000 * 20; nav.target = KIRIN_TARGET_POST;
    Q (r.nav ({}, 20000, nav));
    Q (state.single == held && state.header.target == KIRIN_TARGET_DELTA);
    nav.source.generation++;
    Q (r.nav ({}, 21000, nav));
    Q (!state.single); // Actual source retirement cannot revive the old hit.
    Q (factNumber (.2) == "0.2" && factNumber (1) == "1" && factNumber (1e-9).contains ("e"));
    return true;
}
bool intervalContracts()
{
    Q (formatInterval (lowerBound (146.6), 0, "ms", true, false).value == juce::String::fromUTF8 (u8"≥+146"));
    auto upper = lowerBound (0); upper.lower = { KIRIN_ENDPOINT_NEGATIVE_INFINITY, 0, {}, 0 };
    upper.upper = { KIRIN_ENDPOINT_FINITE, 1, {}, -146.6 };
    Q (formatInterval (upper, 0, "ms", true, false).value == juce::String::fromUTF8 (u8"≤−146"));
    Q (formatInterval (interval (.01, .02, false, true), 1, "ms", true, false).value == "(+0.0,+0.1]");
    Q (formatInterval (interval (-3202.6, 3202.6), 1, "dB", true, false).value == juce::String::fromUTF8 (u8"[−3.21,+3.21]"));
    auto huge = formatInterval (interval (-std::numeric_limits<double>::max(), std::numeric_limits<double>::max()), 1, "dB", true, false);
    Q (huge.valid && huge.exponent == 308 && huge.value == juce::String::fromUTF8 (u8"[−1.80,+1.80]"));
    auto broken = lowerBound (1); broken.upper.closed = 1;
    Q (! formatInterval (broken, 1, "ms", true, false).valid);
    return true;
}
bool evidenceLanguageContracts()
{
    const std::array<const char*, 5> english { "Acquiring", "Complete", "Audio ended", "Not retained", "Retired" };
    const std::array<const char*, 5> japanese { u8"取得中", u8"完了", u8"入力終了", u8"保持外", u8"退役" };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (std::uint8_t finish = 0; finish < 5; ++finish)
        {
            const auto expected = juce::String::fromUTF8 ((language == i18n::Language::japanese ? japanese : english)[finish]);
            Q (finishText (finish) == expected);
            Presentation p; p.header.band = 5;
            auto raw = std::make_shared<KirinAttackSingleSnapshotV2>();
            raw->lanes[0].finish = finish; p.single = raw;
            p.lanes[0].reason = finish == KIRIN_FINISH_AUDIO_END ? KIRIN_REASON_AUDIO_END : KIRIN_REASON_NONE;
            p.lanes[0].count = 8; p.lanes[0].scope = Scope::wholePoint;
            Q (scopeText (p.lanes[0], true).isEmpty());
            juce::Image image (juce::Image::ARGB, 900, 600, true);
            juce::Graphics g (image); text_style::ShownTextLog log;
            paintEvidence (g, p, image.getBounds(), presentation::forEditor (900, 600), 0);
            Q (log.texts().contains (expected));
            Q (finishText (KIRIN_FINISH_AUDIO_END) == reasonText (KIRIN_REASON_AUDIO_END, true));
            for (const auto& text : log.texts())
            {
                Q (! text.contains ("finish ") && ! text.contains ("Whole 8") && ! text.contains (juce::String::fromUTF8 (u8"全8打")));
                Q (! text.contains ("Audio ended / Audio ended")
                    && ! text.contains (juce::String::fromUTF8 (u8"入力終了 / 入力終了"))
                    && ! text.contains (juce::String::fromUTF8 (u8"音声終端")));
            }
            p.lanes[0].number.valid = true;
            Q (scopeText (p.lanes[0], true).isNotEmpty());
            p.lanes[0].scope = Scope::noScalar;
            Q (scopeText (p.lanes[0], true).isEmpty());
            p.lanes[0].reason = KIRIN_REASON_NEXT_HIT;
            paintEvidence (g, p, image.getBounds(), presentation::forEditor (900, 600), 0);
            Q (log.texts().contains (reasonText (KIRIN_REASON_NEXT_HIT, true) + " / " + expected));
        }
    }
    return true;
}
bool summaryContracts()
{
    Rig r; r.view->setBand (5); auto s = std::make_unique<KirinAttackBandSummaryV2> (summary());
    Q (r.nav ({ s->events, s->events + 8 }));
    Q (r.view->setSummarySnapshotV2 (*s, 0));
    const auto& p = r.view->presentationSnapshotV2();
    Q (p.lanes[2].scope == Scope::wholeInterval && p.lanes[2].number.value == juce::String::fromUTF8 (u8"≥+200"));
    Q (p.lanes[0].scope == Scope::confirmedSubset && p.lanes[0].exactCount == 1 && p.lanes[0].count == 8);
    Q (std::abs (p.lanes[0].ageSeconds - 3) < 1e-12 && p.lanes[1].ageSeconds <= 0 && std::abs (p.maximumSubsetAge - 3) < 1e-12);
    Q (p.summary->head[47].participating_bits == 1 && p.summary->head[48].participating_bits == 2
        && ! p.summary->head[48].connect_previous && p.summary->head[49].connect_previous);
    // Hash900 ->2 at a later cutoff must be admitted; fourHz changes every field at once.
    s->header.snapshot_revision = 2; s->header.cutoff_sample += 4800;
    s->lanes[2].whole_interval = lowerBound (146.6);
    Q (r.view->setSummarySnapshotV2 (*s, 125));
    Q (p.summary->header.snapshot_revision == 900 && p.lanes[2].number.value == juce::String::fromUTF8 (u8"≥+200"));
    r.view->advanceCapturePresentationAt (250); Q (p.header.snapshot_revision == 900);
    r.view->setPresentationContext (presentation::forOutput (900, 600, presentation::OutputTarget::capture)); r.view->setSize (872, 412);
    Q (r.view->createComponentSnapshot (r.view->getLocalBounds(), true, 1.25f).isValid() && p.header.snapshot_revision == 900);
    r.view->setSize (580, 248); r.view->setPresentationContext (presentation::forEditor (600, 400));
    r.view->presentationTickAt (250);
    Q (p.header.snapshot_revision == 2 && p.lanes[2].number.value == juce::String::fromUTF8 (u8"≥+146"));
    // A same-cutoff late measurement has another smaller content hash, also admitted.
    s->header.snapshot_revision = 1; s->lanes[2].whole_interval = lowerBound (160.2);
    Q (r.view->setSummarySnapshotV2 (*s, 260));
    Q (p.header.snapshot_revision == 2);
    r.view->presentationTickAt (500); Q (p.header.snapshot_revision == 1);
    auto invalid = *s; invalid.lanes[2].class_count[3] = 1;
    Q (! r.view->setSummarySnapshotV2 (invalid, 510) && p.header.snapshot_revision == 1);
    // Evidence is frozen through subsequent publications, then returns with one End action.
    Q (r.view->keyPressed (juce::KeyPress ('i', {}, 'i')));
    const auto frozen = renderAttack (*r.view);
    s->header.snapshot_revision = 7; Q (r.view->setSummarySnapshotV2 (*s, 750));
    Q (differences (frozen, renderAttack (*r.view)) == 0);
    const auto liveSummary = p.summary; const auto liveRevision = p.revision;
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    Q (p.live && p.summary == liveSummary && p.revision == liveRevision);
    // All five producer classes keep their denominator; neither pending nor N/A becomes zero.
    auto classes = summary(); classes.header.snapshot_revision = 8; classes.header.cutoff_sample = s->header.cutoff_sample;
    auto& l = classes.lanes[3]; std::fill (std::begin (l.class_count), std::end (l.class_count), 0);
    l.class_count[0] = l.class_count[1] = l.class_count[4] = 2; l.class_count[2] = l.class_count[3] = 1;
    l.exact_count = 2; l.exact_median = .5; l.exact_latest_event_sample = classes.events[1].event_sample;
    l.render_kind = KIRIN_RENDER_CONFIRMED_SUBSET; l.whole_median_available = l.whole_numeric_informative = 0;
    const std::array<std::uint8_t, 8> codes {0,0,1,1,2,3,4,4};
    for (std::size_t i = 0; i < 8; ++i) { classes.evidence[i][3].class_code = codes[i]; classes.evidence[i][3].has_interval = codes[i] <= 1; }
    Q (r.view->setSummarySnapshotV2 (classes, 1000));
    Q (p.lanes[3].counts == (std::array<std::uint8_t,5> {2,2,1,1,2}) && p.lanes[3].exactCount == 2 && p.lanes[3].count == 8);
    return true;
}
bool singleAndMotionContracts()
{
    Rig r;
    Q (r.nav ({ key (470000, 1) }));
    auto a = single (r.last, r.view->singleRequestToken());
    a.has_all_post = a.has_all_pre = 1; a.all_post = laneDetail (470000); a.all_pre = a.all_post;
    a.all_post.transient_db = -30; a.finish = KIRIN_FINISH_FULL;
    Q (r.view->setSingleSnapshotV2 (a, 0));
    const auto liveSingle = r.view->presentationSnapshotV2().single;
    const auto liveToken = r.view->singleRequestToken();
    const auto liveRevision = r.view->presentationSnapshotV2().revision;
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    Q (r.view->presentationSnapshotV2().single == liveSingle && r.view->singleRequestToken() == liveToken
        && r.view->presentationSnapshotV2().revision == liveRevision);
    auto h = header(); h.cutoff_sample = 486000;
    Q (r.nav ({ key (470000, 1), key (486000, 2) }, 125, h));
    const auto& p = r.view->presentationSnapshotV2();
    Q (! p.single && r.last.event.event_sample == 486000 && p.viewport < 486000);
    auto b = single (r.last, r.view->singleRequestToken());
    b.has_all_post = b.has_all_pre = 1; b.all_post = laneDetail (486000); b.all_pre = b.all_post;
    b.all_post.transient_db = -10;
    Q (r.view->setSingleSnapshotV2 (b, 125));
    Q (p.single->event.event_sample == 486000 && p.lanes[0].number.value == juce::String::fromUTF8 (u8"−18.0"));
    const auto geometryAt200 = geometry (580, 248, presentation::forEditor (600, 400), true);
    Q (countColour (renderAttack (*r.view), geometryAt200.history, juce::Colour (attack_ui::selectionColour), 20) == 0);
    r.view->presentationTickAt (150); Q (p.viewport == 480000);
    // One locator click locks B before its marker reaches the viewport.
    r.click (geometryAt200.caption.getCentre()); Q (! p.live);
    auto locked = single (r.last, r.view->singleRequestToken());
    locked.has_all_post = locked.has_all_pre = 1; locked.all_post = b.all_post; locked.all_pre = b.all_pre;
    r.view->presentationTickAt (400); Q (p.clock == ClockState::hold);
    locked.finish = KIRIN_FINISH_AUDIO_END; locked.measurement_revision = 2;
    Q (r.view->setSingleSnapshotV2 (locked, 450));
    Q (p.single && p.single->finish == KIRIN_FINISH_AUDIO_END && p.clock == ClockState::hold);
    {
        text_style::ShownTextLog facts;
        Q (r.view->keyPressed (juce::KeyPress ('i', {}, 'i'))); renderAttack (*r.view);
        Q (! facts.texts().joinIntoString ("\n").contains (words ("Requested ", u8"要求窓 ")));
        r.view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    }
    h.cutoff_sample = 900000; Q (r.nav ({}, 600, h));
    Q (p.single && p.single->event.event_sample == 486000 && ! p.live);
    const auto terminalRevision = p.revision;
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::leftKey)) && p.revision == terminalRevision && p.single);
    locked.measurement_revision = 3; locked.all_post.transient_db = 77;
    Q (! r.view->setSingleSnapshotV2 (locked, 601) && p.revision == terminalRevision);
    // Band change retains the same EventKey and starts a different keyed measurement.
    r.view->setBand (5); Q (! p.live && ! p.single);
    Q (r.nav ({}, 610, h) && r.last.event.event_sample == 486000 && r.last.band == 5);
    h.target = KIRIN_TARGET_POST; Q (r.nav ({}, 615, h));
    Q (! p.live && ! p.single && r.last.event.event_sample == 486000 && r.last.target == KIRIN_TARGET_POST);
    h.target = KIRIN_TARGET_DELTA; Q (r.nav ({}, 616, h));
    Q (! p.live && r.last.event.event_sample == 486000 && r.last.target == KIRIN_TARGET_DELTA);
    h.source.incarnation[0]++; Q (r.nav ({}, 620, h));
    Q (p.live && ! p.single && r.view->singleRequestToken() == 0);
    Q (! r.view->setSingleSnapshotV2 (locked, 630));
    Q (r.view->capturePresentationStamp().requiresTypedMetadata);
    return true;
}
bool clusterContracts()
{
    Rig r; const auto shape = geometry (580, 248, presentation::forEditor (600, 400), true);
    std::vector<KirinSnapshotEventKey> hits { key (400000, 1), key (400010, 2), key (400020, 3), key (470000, 4) };
    Q (r.nav (hits));
    const auto x = juce::roundToInt (eventX (400000, 472800, 48000, shape.history));
    r.click ({ x, shape.history.getCentreY() }); Q (r.last.event.token == 1);
    {
        text_style::ShownTextLog cluster; renderAttack (*r.view);
        Q (cluster.texts().joinIntoString ("\n").containsChar (0x0394));
        Q (! cluster.texts().joinIntoString ("\n").containsChar (0x00ce));
    }
    const auto count = r.requested.size();
    r.view->mouseDrag (mouse (*r.view, { static_cast<float> (shape.history.getRight()), static_cast<float> (shape.history.getCentreY()) }));
    Q (r.requested.size() == count && r.last.event.token == 1);
    hits.insert (hits.begin() + 3, key (400030, 5)); auto h = header(); h.cutoff_sample += 4800;
    Q (r.nav (hits, 100, h));
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)) && r.last.event.token == 2);
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)) && r.last.event.token == 3);
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)) && r.last.event.token == 1); // late fourth never enters frozen candidates
    r.view->setPresentationContext (presentation::forEditor (900, 600)); r.view->setSize (872, 412);
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::leftKey)) && r.last.event.token == 3);
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)) && ! r.view->presentationSnapshotV2().live);
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)) && r.view->presentationSnapshotV2().live);
    // A normal drag keeps the original hit set while publications add a nearer late event.
    Rig d; Q (d.nav ({ key (400000, 1), key (470000, 2) }));
    d.click ({ x, shape.history.getCentreY() });
    Q (d.nav ({ key (400000, 1), key (450000, 3), key (470000, 2) }, 100, h));
    d.view->mouseDrag (mouse (*d.view, { eventX (450000, 472800, 48000, shape.history), static_cast<float> (shape.history.getCentreY()) }));
    Q (d.last.event.token == 2);
    Rig busy; busy.busy = true; Q (busy.nav ({ key (400000, 1) }));
    busy.click ({ x, shape.history.getCentreY() }); Q (! busy.view->presentationSnapshotV2().live && busy.view->singleRequestToken() == 0);
    busy.view->presentationTickAt (30); Q (busy.last.event.token == 1 && ! busy.view->presentationSnapshotV2().live);
    const auto requestsBeforeCapture = busy.requested.size(); busy.view->advanceCapturePresentationAt (40);
    busy.view->setPresentationContext (presentation::forOutput (900, 600, presentation::OutputTarget::capture)); busy.view->setSize (872, 412);
    const auto stampBeforeRender = busy.view->capturePresentationStamp();
    Q (busy.view->createComponentSnapshot (busy.view->getLocalBounds(), true, 1.25f).isValid()
        && busy.view->capturePresentationStamp().presentationRevision == stampBeforeRender.presentationRevision);
    busy.view->setSize (580, 248); busy.view->setPresentationContext (presentation::forEditor (600, 400));
    Q (busy.requested.size() == requestsBeforeCapture && busy.view->singleRequestToken() == 0);
    busy.busy = false; busy.view->presentationTickAt (60); Q (busy.view->singleRequestToken() != 0 && busy.last.event.token == 1);
    const auto acceptedToken = busy.view->singleRequestToken();
    auto unusable = header(); unusable.version = 99; Q (! busy.nav ({}, 70, unusable));
    Q (busy.view->singleRequestToken() == acceptedToken && ! busy.view->presentationSnapshotV2().live);
    busy.view->retireV2(); Q (busy.view->singleRequestToken() == 0 && busy.view->presentationSnapshotV2().live);
    for (const auto& lane : busy.view->presentationSnapshotV2().lanes)
        Q (lane.number.value == "---" && lane.number.unit.isNotEmpty() && lane.count == 0 && lane.reason == KIRIN_REASON_SOURCE_CHANGED);
    busy.view->beginSnapshotV2();
    for (const auto& lane : busy.view->presentationSnapshotV2().lanes)
        Q (lane.count == 0 && lane.reason == KIRIN_REASON_WAITING_SERVICE);
    Q (busy.nav ({})); Q (busy.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    Q (busy.view->presentationSnapshotV2().lanes[0].count == 0);
    return true;
}
bool clockContracts()
{
    ViewportClock clock; clock.observe (480000, 48000, 0, true, true);
    Q (clock.position() == 472800); clock.observe (484800, 48000, 100, true, true);
    clock.tick (150); Q (clock.position() == 480000);
    clock.observe (489600, 48000, 190, true, true); clock.tick (200); Q (clock.position() == 482400);
    clock.observe (494400, 48000, 310, true, true); clock.tick (350); Q (clock.position() == 489600);
    clock.tick (600); Q (clock.status() == ClockState::hold && clock.position() <= clock.ceiling());
    clock.observe (508800, 48000, 700, true, true); Q (clock.position() == 501600);
    clock.observe (513600, 48000, 790, true, true); clock.tick (800); Q (clock.position() == 506400);
    clock.observe (520000, 48000, 850, true, false); Q (clock.status() == ClockState::hold);
    clock.observe (520000, 48000, 900, true, false); Q (clock.status() == ClockState::hold);
    return true;
}
bool placeholderContracts()
{
    Rig r; r.view->setBand (5); auto s = std::make_unique<KirinAttackBandSummaryV2> (summary());
    Q (r.nav ({ s->events, s->events + 8 }) && r.view->setSummarySnapshotV2 (*s, 0));
    r.view->setBand (4);
    const auto waiting = [&] (const std::array<const char*, 4>& units, std::uint8_t reason) {
        const auto& p = r.view->presentationSnapshotV2();
        if (p.single || p.summary || ! p.live) return false;
        for (std::size_t i = 0; i < units.size(); ++i)
            if (p.lanes[i].number.value != "---" || p.lanes[i].number.unit != units[i]
                || p.lanes[i].count != 0 || p.lanes[i].scope != Scope::noScalar || p.lanes[i].reason != reason) return false;
        return true;
    };
    Q (waiting ({ "ms", "ms", "ms", "dB" }, KIRIN_REASON_WAITING_SERVICE));
    auto h = header(); h.target = KIRIN_TARGET_POST;
    Q (r.nav ({ s->events, s->events + 8 }, 1, h));
    Q (waiting ({ "ms", "ms", "ms", "dBFS" }, KIRIN_REASON_WAITING_SERVICE));
    r.view->retireV2(); Q (waiting ({ "ms", "ms", "ms", "dBFS" }, KIRIN_REASON_SOURCE_CHANGED));
    r.view->beginSnapshotV2(); Q (waiting ({ "ms", "ms", "ms", "dBFS" }, KIRIN_REASON_WAITING_SERVICE));
    h.target = KIRIN_TARGET_DELTA; Q (r.nav ({}, 2, h));
    Q (waiting ({ "ms", "ms", "ms", "dB" }, KIRIN_REASON_WAITING_SERVICE));
    Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    Q (waiting ({ "ms", "ms", "ms", "dB" }, KIRIN_REASON_WAITING_SERVICE));
    // Re-clicking an older LOCK sets the selection's LIVE intent before goLive is called.
    // It must request the actual newest hit rather than retain the old keyed measurement.
    Rig all; Q (all.nav ({ key (400000, 1), key (470000, 2) }));
    const auto shape = geometry (580, 248, presentation::forEditor (600, 400), true);
    const juce::Point<int> hit { juce::roundToInt (eventX (400000, 472800, 48000, shape.history)), shape.history.getCentreY() };
    all.click (hit); Q (! all.view->presentationSnapshotV2().live && all.last.event.token == 1);
    all.click (hit); Q (all.view->presentationSnapshotV2().live && all.last.event.token == 2);
    return true;
}
bool cohortPointerContracts()
{
    const std::array<int, 5> widths {292,363,434,580,872}, heights {120,158,164,248,412}, editors {300,375,450,600,900};
    for (std::size_t size = 0; size < widths.size(); ++size)
        for (const auto classification : { KIRIN_SCALAR_PENDING, KIRIN_SCALAR_UNKNOWN, KIRIN_SCALAR_NOT_APPLICABLE })
    {
        Rig r; const auto c = presentation::forEditor (editors[size], editors[size]*2/3);
        r.view->setSize (widths[size], heights[size]); r.view->setPresentationContext (c); r.view->setBand (5);
        auto s = std::make_unique<KirinAttackBandSummaryV2> (summary());
        for (auto& lane : s->lanes)
        {
            std::fill (std::begin (lane.class_count), std::end (lane.class_count), 0);
            std::fill (std::begin (lane.reason_count), std::end (lane.reason_count), 0);
            lane.class_count[classification] = 8; lane.exact_count = 0;
            lane.whole_median_available = lane.whole_numeric_informative = 0; lane.render_kind = KIRIN_RENDER_NO_SCALAR;
            if (classification != KIRIN_SCALAR_NOT_APPLICABLE)
                lane.reason_count[classification == KIRIN_SCALAR_PENDING ? KIRIN_REASON_WAITING_SERVICE : KIRIN_REASON_CLOCK] = 8;
        }
        for (auto& row : s->evidence) for (auto& e : row) { e.class_code = static_cast<std::uint8_t> (classification); e.has_interval = 0;
            e.reason = classification == KIRIN_SCALAR_PENDING ? KIRIN_REASON_WAITING_SERVICE : classification == KIRIN_SCALAR_UNKNOWN ? KIRIN_REASON_CLOCK : KIRIN_REASON_NONE; }
        for (auto& p : s->head) p = {}; for (auto& p : s->tail) p = {};
        std::vector<KirinSnapshotEventKey> keys {key (300000, 99)}; keys.insert (keys.end(), s->events, s->events + 8);
        Q (r.nav (keys) && r.view->setSummarySnapshotV2 (*s, 0));
        const auto shape = geometry (widths[size], heights[size], c);
        const auto point = [&] (std::size_t i) { return juce::Point<int> {
            juce::roundToInt (eventX (s->events[i].event_sample, s->header.cutoff_sample, 48000, shape.history)), shape.history.getCentreY() }; };
        std::cout << "DRUM V2 cohort pointer editor=" << editors[size] << " class=" << classification
            << " strip=" << shape.history.toString() << " C=" << s->header.cutoff_sample << '\n';
        const auto select = [&] (std::size_t i) {
            r.click (point (i));
            // At compact sizes 100ms spacing is narrower than the pick radius. Exercise the
            // real frozen cluster rather than moving markers or relaxing that radius.
            for (int attempt = 0; attempt < 8 && r.last.event.token != s->events[i].token; ++attempt)
                r.view->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
            std::cout << "  pointer=" << i + 1 << " requested=" << r.last.event.token << " target=" << static_cast<unsigned> (r.last.target) << '\n';
            return ! r.view->presentationSnapshotV2().live && r.last.event.token == s->events[i].token;
        };
        Q (r.view->getLocalBounds().contains (shape.history) && shape.history.contains (point (7)));
        Q (select (0) && r.last.band == 5);
        auto one = single (r.last, r.view->singleRequestToken()); Q (r.view->setSingleSnapshotV2 (one, 1));
        Q (! r.view->presentationSnapshotV2().summary && r.view->presentationSnapshotV2().lanes[0].number.value == "---");
        auto h = header(); h.cutoff_sample = 900000; Q (r.nav ({key (899000, 100)}, 200, h));
        // LOCK keeps the original C/6s cohort despite a later navigation publication.
        for (std::size_t i = 1; i < 8; ++i) Q (select (i));
        Q (select (0));
        h.target = KIRIN_TARGET_POST; Q (r.nav ({key (899000, 100)}, 210, h));
        Q (select (7) && r.last.target == KIRIN_TARGET_POST);
        for (const auto& request : r.requested) Q (request.event.token != 99 && request.event.token != 100);
        r.view->mouseUp (mouse (*r.view, point (7).toFloat()));
        Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)) && ! r.view->presentationSnapshotV2().live);
        Q (r.view->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)) && r.last.event.token == 100);
        if (size >= 3)
        {
            Q (! shape.head.intersects (shape.history) && ! shape.tail.intersects (shape.history));
            Q ((shape.head.getHeight() - 4 - (size == 4 ? 20 : 14) - 4) / 2 >= 24);
        }
    }
    // The visible finite-axis pool remains available after summary→Single, allowing re-click LIVE.
    Rig r; r.view->setBand (5); auto s = std::make_unique<KirinAttackBandSummaryV2> (summary());
    for (std::size_t i = 0; i < 8; ++i) { auto& e = s->evidence[i][3]; const auto value = i == 0 ? -9.0 : 4.0 + static_cast<double> (i);
        e.interval = interval (value, value); e.interval.unit = KIRIN_INTERVAL_DECIBELS; }
    s->lanes[3].whole_interval = interval (7.5, 7.5); s->lanes[3].whole_interval.unit = KIRIN_INTERVAL_DECIBELS; s->lanes[3].exact_median = 7.5;
    Q (r.nav ({s->events, s->events + 8}) && r.view->setSummarySnapshotV2 (*s, 0));
    const auto shape = geometry (580, 248, presentation::forEditor (600, 400)); const auto axis = shape.lanes[3].axis;
    const juce::Point<int> hit { axis.getX() + juce::roundToInt (static_cast<float> (axis.getWidth() - 1) * 3.0f/24.0f), axis.getY() + 8 };
    r.click (hit); Q (! r.view->presentationSnapshotV2().live && r.last.event.token == 1);
    r.click (hit); Q (r.view->presentationSnapshotV2().live);
    return true;
}
bool frameBudget()
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_FRAME_BUDGET", {}).isEmpty()) return true;
    material_cache::Lifetime lifetime;
    const std::array<int, 5> widths {292,363,434,580,872}, heights {120,158,164,248,412}, editors {300,375,450,600,900};
    for (std::size_t size = 0; size < 5; ++size) for (auto dpi : {1.0f,1.25f,2.0f})
        for (auto instances : {1,2}) for (auto band : {0,5}) for (auto overlay : {false,true})
    {
        std::array<Rig, 2> rigs;
        auto wave = std::make_unique<KirinAttackWaveformBatch>(); wave->count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
        auto s = std::make_unique<KirinAttackBandSummaryV2> (denseSummary());
        Q (validSummary (*s));
        std::vector<KirinSnapshotEventKey> hits;
        for (std::size_t n = 0; n < 240; ++n) hits.push_back (key (192000 + static_cast<std::int64_t> (n) * 1200, n + 1));
        for (int i = 0; i < instances; ++i) { rigs[static_cast<std::size_t> (i)].view->setPresentationContext (presentation::forEditor (editors[size], editors[size]*2/3)); rigs[static_cast<std::size_t> (i)].view->setSize (widths[size], heights[size]); rigs[static_cast<std::size_t> (i)].view->setBand (static_cast<std::uint8_t> (band)); rigs[static_cast<std::size_t> (i)].view->setOverlayMode (overlay); }
        juce::Image image (juce::Image::ARGB, juce::roundToInt (static_cast<float> (widths[size])*dpi), juce::roundToInt (static_cast<float> (heights[size])*dpi), true);
        const auto frame = [&] (int n) {
            const auto now = n * (1000.0 / 30); auto h = header(); h.cutoff_sample += n * 1600;
            hits.back() = key (h.cutoff_sample - 960, static_cast<std::uint64_t> (n + 1000));
            for (std::uint32_t j = 0; j < wave->count; ++j)
            {
                auto& p = wave->points[j]; p.generation = 7; p.sample_rate = 48000; p.channels = 2;
                p.start_sample = h.cutoff_sample - 288000 + j * 480; p.end_sample = p.start_sample + 480;
                p.rms_dbfs = -30 + 12 * std::sin (static_cast<float> (j) * .1f + static_cast<float> (n) * .08f);
            }
            for (int i = 0; i < instances; ++i)
            {
                auto& rig = rigs[static_cast<std::size_t> (i)]; rig.view->setNavigationV2 (h, hits, *wave, *wave, now);
                if (band != 0)
                { s->header.cutoff_sample = h.cutoff_sample; s->header.snapshot_revision = static_cast<std::uint64_t> (n + 1); rig.view->setSummarySnapshotV2 (*s, now); }
                else
                {
                    auto one = single (rig.last, rig.view->singleRequestToken()); one.has_all_post = one.has_all_pre = 1;
                    one.all_post = laneDetail (rig.last.event.event_sample); one.all_pre = one.all_post;
                    one.finish = KIRIN_FINISH_FULL; rig.view->setSingleSnapshotV2 (one, now);
                }
                rig.view->presentationTickAt (now);
                juce::Graphics g (image); g.addTransform (juce::AffineTransform::scale (dpi)); rig.view->paintEntireComponent (g, true);
            }
        };
        const auto coldStart = juce::Time::getMillisecondCounterHiRes(); frame (0);
        const auto cold = juce::Time::getMillisecondCounterHiRes() - coldStart;
        std::array<double, 9> samples {};
        for (int n = 1; n <= 9; ++n) { const auto start = juce::Time::getMillisecondCounterHiRes(); frame (n); samples[static_cast<std::size_t> (n-1)] = juce::Time::getMillisecondCounterHiRes() - start; }
        std::sort (samples.begin(), samples.end());
        std::cout << "DRUM V2 changing frame size=" << editors[size] << " dpi=" << dpi << " instances=" << instances
            << " ALLkeys=240 band=" << band << " overlay=" << overlay << " cold_ms=" << cold << " median_ms=" << samples[4] << " max_ms=" << samples[8] << '\n';
#if ! JUCE_DEBUG
        Q (cold <= 80 && samples[4] <= (instances == 1 ? 12 : 16) && samples[8] <= 24);
#endif
    }
    return true;
}
bool sizesAndArtifacts()
{ return v2_main_surface_contract::verify<Rig>(); }
}
bool verifyAttackV2PresentationContract()
{
    const auto focused = juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_V2_FOCUSED", {});
    if (focused == "cohort") return cohortPointerContracts();
    if (focused == "layout") return sizesAndArtifacts();
    return completedLockSurvivesLiveWindow() && frameBudget() && verifyAttackV2MaterialContract() && intervalContracts() && evidenceLanguageContracts() && summaryContracts() && singleAndMotionContracts()
        && clusterContracts() && clockContracts() && placeholderContracts() && cohortPointerContracts() && sizesAndArtifacts();
}
}
