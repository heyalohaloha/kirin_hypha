#include "BinaryData.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaMetricFacePainter.h"
#include "../src/HyphaPreSingleDialPreview.h"
#include "../src/HyphaVuNeedlePainter.h"
#include <cstdlib>
#include "../src/HyphaTextStyle.h"
#include <iostream>
#include <stdexcept>

namespace hypha::tests
{
namespace
{
using metric_needle::Face;
using metric_needle::Frame;
using metric_needle::Status;
juce::String context;
void check (bool condition, const char* message)
{
    if (!condition) throw std::runtime_error (context.toStdString() + ": " + message);
}
KirinMeterSession meter()
{
    KirinMeterSession value {};
    value.state = KIRIN_METER_SESSION_ACTIVE;
    value.channels = 2; value.sample_rate = 48000;
    value.lufs_m = -12.5; value.lufs_s = -13.5; value.lufs_i = -14.5;
    value.true_peak = -3.2; value.max_true_peak = -1.2;
    for (int channel = 0; channel < 2; ++channel)
    {
        value.channel_vu_dbfs[channel] = -28 + channel;
        value.channel_instant_true_peak_dbtp[channel] = -18 + channel;
        value.channel_max_true_peak_dbtp[channel] = -3.2;
        value.channel_clip_latched[channel] = 1;
    }
    return value;
}
lu_needle::Facts reference()
{
    lu_needle::Facts facts;
    facts.readable = true; facts.zero = lu_needle::Zero::ready;
    facts.zeroLufs = -14; facts.song = "North Road";
    return facts;
}
juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::RGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics g (image); g.fillAll (juce::Colours::black);
    view.paintEntireComponent (g, true);
    return image;
}
int differences (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area)
{
    int result = 0;
    area = area.getIntersection (a.getBounds());
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt (x,y) != b.getPixelAt (x,y)) ++result;
    return result;
}
void preview (const juce::Image& image, const juce::String& name)
{
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    if (directory.isEmpty()) return;
    const juce::File folder (directory);
    check (folder.createDirectory().wasOk(), "create PNG directory");
    auto stream = folder.getChildFile (name + ".png").createOutputStream();
    check (stream != nullptr && stream->setPosition (0) && stream->truncate().wasOk(), "open PNG");
    check (juce::PNGImageFormat().writeImageToStream (image, *stream), "write PNG");
}
void verifyIndependentPreDial (int width, const Frame& frame)
{
    namespace pre = metric_face::pre_preview;
    const juce::Rectangle<int> canvas (width, width * 2 / 3);
    const auto chassis = pre::chassisBounds (canvas);
    check (chassis == canvas, "3:2 backgrounds keep original scale");
    const auto fitted = pre::chassisBounds (canvas.withHeight (canvas.getHeight() - 7));
    check (fitted.getWidth() * 2 == fitted.getHeight() * 3, "non-3:2 outer bounds still fit uniformly");
    const auto centre = pre::singleDial (chassis).pivot;
    check (std::abs (centre.x - chassis.getCentreX()) < 1.0f, "PRE one vector dial is centred");
    const auto masks = pre::hubMasks (chassis);
    check (masks[0].getRight() < centre.x && masks[1].getX() > centre.x, "masks cover only the old hub regions");
    KirinMeterSession meter {};
    KirinWatchDisplay watch {};
    hybrid_vu::State state { observatory::Role::pre, meter, watch };
    state.presentation = presentation::forEditor (width, canvas.getHeight());
    juce::Image base (juce::Image::RGB, width, canvas.getHeight(), true);
    { juce::Graphics g (base); hybrid_vu::paintStatic (g, chassis, state); }
    const auto masked = pre::withLocalGrainCovers (base);
    const int protectedRow = juce::roundToInt (chassis.getHeight() * pre::protectedGoldRow /
                                               static_cast<float> (pre::chassisAssetHeight));
    for (int y = 0; y < canvas.getHeight(); ++y)
        for (int x = 0; x < width; ++x)
            if (y >= protectedRow || (! masks[0].getSmallestIntegerContainer().contains (x, y)
                && ! masks[1].getSmallestIntegerContainer().contains (x, y)))
                check (base.getPixelAt (x, y) == masked.getPixelAt (x, y), "frame/texture outside the two local masks is pixel-identical");
    check (differences (base, masked, masks[0].getSmallestIntegerContainer()) > 0
        && differences (base, masked, masks[1].getSmallestIntegerContainer()) > 0,
        "both old hub regions are affected; a transparent/no-op mask is not a pass");
    auto sample = frame;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const auto lang = language == i18n::Language::english ? "en" : "ja";
        for (auto which : { "ready", "preparing", "zero", "range-out", "unavailable" })
        {
            sample = frame;
            if (juce::String (which) == "preparing") sample.status = Status::measuring;
            if (juce::String (which) == "unavailable") sample.status = Status::unavailable;
            if (juce::String (which) == "zero") sample.own = frame.median;
            if (juce::String (which) == "range-out") sample.own = frame.median + 2.0 * metric_needle::extent (frame.face);
            juce::Image image (juce::Image::RGB, width, canvas.getHeight(), true);
            juce::Graphics g (image);
            pre::paint (g, canvas, state, sample);
            preview (image, juce::String ("pre-single-native-") + metric_needle::label (frame.face)
                     + "-" + lang + "-" + which + "-" + juce::String (width));
        }
    }
}
void verifyChassisSelectionAndStaticCache()
{
    using metric_face::MetricChassisPair;
    check (metric_face::dialCount (observatory::Role::pre) == 1
            && metric_face::dialCount (observatory::Role::post) == 2, "PRE B and POST A share one role-count rule");
    MetricChassisPair bank;
    bank.pre.image = juce::Image (juce::Image::RGB, 1536, 1024, true);
    check (! bank.isValid(), "do not enable an incomplete mid pair");
    bank.post = juce::Image (juce::Image::RGB, 1536, 1024, true);
    check (bank.isValid(), "complete explicit fixtures enable mid only in this test");
    KirinMeterSession meter {};
    KirinWatchDisplay watch {};
    const auto box = juce::Rectangle<int> (600, 400);
    juce::Image layer (juce::Image::RGB, 900, 600, true);
    juce::Graphics g (layer);
    metric_face::MeterBackdrop backdrop;
    auto frame = Frame {};
    const hybrid_vu::State state { observatory::Role::pre, meter, watch };
    check (! metric_face::paintMetricCandidate (g, box, state, frame, nullptr, backdrop)
            && backdrop.buildsForTest() == 0, "no selected artwork touches no legacy pixels");
    check (metric_face::paintMetricCandidate (g, box, state, frame, &bank, backdrop)
            && backdrop.buildsForTest() == 1, "initial candidate draws one static raster");
    frame.status = Status::measuring;
    metric_face::paintMetricCandidate (g, box, state, frame, &bank, backdrop);
    check (backdrop.buildsForTest() == 1, "new measurement does not rerasterize chassis");
    metric_face::paintMetricCandidate (g, layer.getBounds(), state, frame, &bank, backdrop);
    check (backdrop.buildsForTest() == 2, "changed size invalidates candidate raster exactly once");
    frame.face = Face::vu;
    const hybrid_vu::State post { observatory::Role::post, meter, watch };
    check (! metric_face::paintMetricCandidate (g, box, post, frame, &bank, backdrop)
            && backdrop.buildsForTest() == 2, "POST VU is never redirected by the metric candidate");
}
std::shared_ptr<const metric_face::MetricChassisPair> embeddedMid()
{
    using namespace BinaryData;
    auto image = [] (const void* data, int n) { return juce::ImageFileFormat::loadFrom (data, static_cast<std::size_t> (n)); };
    auto mid = std::make_shared<const metric_face::MetricChassisPair> (metric_face::MetricChassisPair {
        {image (hybrid_vu_chassis_single_png, hybrid_vu_chassis_single_pngSize), {1531.0f, 1251.0f}},
        image (hybrid_vu_chassis_png, hybrid_vu_chassis_pngSize)});
    check (mid->isValid() && mid->post.getWidth() == 1536 && mid->post.getHeight() == 1024,
           "use the embedded shipping images, not surrogate colours or a missing bank");
    return mid;
}

void verifyEmbeddedMidReference()
{
    namespace mf = metric_face;
    const auto mid = embeddedMid();
    for (const auto role : {observatory::Role::pre, observatory::Role::post})
    for (const auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        const i18n::ScopedLanguage scoped (language);
        const bool pre = role == observatory::Role::pre;
        context = pre ? "embedded PRE PSR" : "embedded POST PSR";
        observatory::View view (role, mid);
        view.setSize (600, 400);
        view.setMeterSnapshot (meter(), true);
        view.setManualHybridVuVisible (true);
        view.lu().setFacts (reference());
        view.setMetricFace (Face::psr);
        check (view.metricFaceActive(), "embedded metric must actually be selected");
        Frame frame; frame.face = Face::psr; frame.status = Status::ready; frame.ownIsPre = pre;
        frame.median = 12.0; frame.own = 14.25; frame.pre = 10.75; frame.song = "Synthetic Cue";
        const auto imageFor = [&] (const Frame& sample) { view.setMetricFrame (sample); return render (view); };
        const auto plain = imageFor (frame);
        const auto roi = [] (juce::Point<float> p) { return juce::Rectangle<int> (9, 9).withCentre (p.roundToInt()); };
        const auto chassis = mf::pre_preview::chassisBounds (view.getLocalBounds());
        const auto own = mf::candidateDial (chassis, *mid, role, pre ? 0 : 1);
        // An absent FOLLOW value is different from zero; only the admitted display fact changes here.
        if (! pre)
        {
            auto followed = frame; followed.reference = 22.5;
            const auto at = *metric_needle::normalized (Face::psr, 10.5);
            const auto refPoint = [&] (auto geometry) {
                return geometry.pivot + (mf::candidatePoint (geometry, at) - geometry.pivot) * .60f;
            };
            const auto followedImage = imageFor (followed);
            check (differences (plain, followedImage, roi (refPoint (own))) > 0, "FOLLOW A displays a REF needle on POST");
            check (differences (plain, followedImage, roi (refPoint (mf::candidateDial (chassis, *mid, role, 0)))) == 0,
                   "FOLLOW A must not add its needle to the paired PRE dial");
            frame = followed;
        }
        const auto noBand = imageFor (frame);
        frame.band = {-6, 4};  // differences −18 and −8: visibly away from own and REF needles
        const auto middle = *metric_needle::normalized (Face::psr, -13.0);
        text_style::ShownTextLog log;
        const auto withBand = imageFor (frame);
        check (differences (noBand, withBand, roi (mf::candidateReferenceBandPoint (own, middle))) > 0,
               "finished set shows its band on the current dial");
        if (!pre)
            check (differences (noBand, withBand, roi (mf::candidateReferenceBandPoint (mf::candidateDial (chassis, *mid, role, 0), middle))) == 0,
                   "set band is not on the paired PRE dial");
        check (log.texts().contains ("0 = 12.00 dB") && log.texts().contains ("Synthetic Cue"), "zero and Cue title");
        check (log.texts().contains (pre ? "PRE +2.25" : "POST +2.25"), "signed own difference");
        if (!pre) check (log.texts().contains ("PRE -1.25") && log.texts().contains ("REF +10.50"), "signed independent comparisons");
        log.clear();
        frame.status = Status::unavailable; frame.reason = "read_failed";
        imageFor (frame);
        check (log.texts().contains (i18n::tr ("Audio could not be read")), "known failure appears in the requested language");
        log.clear();
        frame.reason = "/private/fixture-placeholder.wav";
        imageFor (frame);
        check (!log.texts().joinIntoString (" ").contains (juce::String (frame.reason.c_str())), "unknown reason never exposes its raw value");
    }
}

void controls (observatory::View& view)
{
    check (view.lu().controlFontHeightForTest() >= 12, "control font floor");
    std::vector<juce::Component*> visible;
    for (const auto* id : {"observatory-hybrid-vu", "observatory-clear-peak-clip", "observatory-lu-face", "observatory-lu-window"})
        if (auto* component = view.findChildWithID (id); component && component->isVisible())
        {
            check (view.getLocalBounds().contains (component->getBounds()), "control inside real View");
            for (const auto* other : visible)
                check (!component->getBounds().intersects (other->getBounds()), "controls never overlap");
            visible.push_back (component);
        }
}
void verify (observatory::Role role, int width, Face face)
{
    const bool pre = role == observatory::Role::pre;
    const int height = width * 2 / 3;
    context = juce::String (pre ? "PRE " : "POST ") + metric_needle::label (face) + " " + juce::String (width);
    observatory::View view (role);
    view.setSize (width,height);
    view.setConnection (pre ? "SOURCE PRE" : "PAIR MIX", COL_LED_BLUE,
                        pre ? observatory::ConnectionState::source : observatory::ConnectionState::paired);
    view.setMeterSnapshot (meter(), true);
    view.setManualHybridVuVisible (true);
    int callbacks = 0;
    view.onMetricFaceChoice = [&] (Face) { ++callbacks; };
    view.setMetricFace (Face::vu);
    const auto plain = render (view);
    view.setMetricFace (face);
    check (callbacks == 0, "restore never emits user-choice callback");
    check (!view.metricFaceActive() && view.metricFaceSelection() == face, "unread Reference retains selection");
    check (differences (plain, render (view), plain.getBounds()) == 0, "unread Reference is unchanged VU fallback");
    view.lu().setFacts (reference());
    if (pre && face == Face::sharp)
    {
        check (!view.metricFaceActive(), "PRE SHARP unavailable by default");
        preview (render (view), "pre-sharp-hidden-" + juce::String (width));
        // Test fixture only. This does not unlock the product's measured CPU gate.
        view.setPreSharpAvailable (true);
    }
    check (view.metricFaceActive() && !view.luFaceShown(), "metric replaces LU");
    check (!view.lu().windowControl().isVisible(), "M/S belongs only to LU");
    Frame frame; frame.face = face; frame.status = Status::ready; frame.ownIsPre = pre;
    frame.median = face == Face::sharp ? 1.5 : 12.0;
    const auto range = metric_needle::extent (face);
    frame.own = frame.median + range * .55;
    frame.pre = frame.median - range * .55;
    frame.reference = frame.median;
    frame.band = {frame.median - range * .25, frame.median + range * .25};
    frame.song = "North Road";
    if (pre) verifyIndependentPreDial (width, frame);
    view.setMetricFrame (frame);
    text_style::ShownTextLog log;
    const auto full = render (view);
    check (log.texts().joinIntoString (" ").contains ("0 ="), "real View names its zero");
    controls (view);
    const auto dial = hybrid_vu::needle_painter::faceBounds (view.getLocalBounds()).toNearestInt();
    for (int which = 0; which < 3; ++which)
    {
        auto absent = frame;
        const auto value = which == 0 ? frame.own : which == 1 ? frame.pre : frame.reference;
        if (which == 0) absent.own = metric_needle::missing;
        if (which == 1) absent.pre = metric_needle::missing;
        if (which == 2) absent.reference = metric_needle::missing;
        view.setMetricFrame (absent);
        const auto without = render (view);
        const auto point = metric_face::needlePoint (dial, *metric_needle::normalized (face, value-frame.median), .65f, true);
        const auto roi = juce::Rectangle<int> (juce::roundToInt (point.x)-2, juce::roundToInt (point.y)-2,5,5);
        const auto changed = differences (full, without, roi);
        check (which == 1 && pre ? changed == 0 : changed > 0, "each live needle exists independently; PRE has no duplicate PRE needle");
    }
    auto noBand = frame; noBand.band = {};
    view.setMetricFrame (noBand);
    const auto unbanded = render (view);
    const auto bandPoint = metric_face::needlePoint (dial, .58f, .94f, true);
    check (differences (full, unbanded, {juce::roundToInt (bandPoint.x)-2,juce::roundToInt (bandPoint.y)-2,5,5}) > 0,
           "set band is visibly painted in the real View");
    view.setMetricFrame (frame);
    const auto previewName = juce::String (pre ? "pre" : "post") + "-metric-" + metric_needle::label (face)
        + (pre && face == Face::sharp ? "-fixture-only" : "") + "-" + juce::String (width);
    preview (render (view), previewName);
    // Keep standalone font/placement checks alongside the actual View image.
    std::vector<metric_face::TextBox> trace;
    juce::Image layer (juce::Image::RGB,width,height,true); juce::Graphics graphics (layer);
    metric_face::paint (graphics,dial,frame,presentation::forEditor (width,height),&trace,true);
    for (std::size_t i=0; i<trace.size(); ++i)
    {
        check (trace[i].fontHeight >= 12 && dial.toFloat().contains (trace[i].area), "metric font and bounds");
        for (std::size_t j=i+1; j<trace.size(); ++j)
            check (!trace[i].area.intersects (trace[j].area), "metric text areas disjoint");
    }
    for (const auto status : {Status::measuring, Status::unavailable})
    {
        auto pending = frame; pending.status = status; view.setMetricFrame (pending); log.clear();
        const auto pendingImage = render (view);
        const auto dash = juce::String::charToString (0x2014);
        check (log.texts().contains (juce::String (pre ? "PRE " : "POST ") + dash), "own missing readout uses Unicode U+2014");
        check (log.texts().contains ("REF " + dash), "REF missing readout uses Unicode U+2014");
        if (!pre) check (log.texts().contains ("PRE " + dash), "paired PRE missing readout uses Unicode U+2014");
        const auto shown = log.texts().joinIntoString (" ");
        for (const auto bad : {0x00e2, 0x0080, 0x0094, 0xfffd})
            check (!shown.containsChar (static_cast<juce::juce_wchar> (bad)), "missing readout never contains UTF-8 mojibake or replacement characters");
        preview (pendingImage, previewName + (status == Status::measuring ? "-measuring" : "-unavailable"));
        check (log.texts().joinIntoString (" ").contains (status == Status::measuring ? "PREPARING" : "UNAVAILABLE"), "real View explains pending/missing zero");
    }
    {
        auto failed = frame; failed.status = Status::unavailable; failed.reason = "source_changed";
        view.setMetricFrame (failed); log.clear(); render (view);
        check (log.texts().joinIntoString (" ").contains ("Audio source changed"), "real View explains the concrete missing reason");
        check (!log.texts().joinIntoString (" ").contains ("source_changed"), "real View hides diagnostic code");
        failed.reason = "/private/user/music.wav"; view.setMetricFrame (failed); log.clear(); render (view);
        check (!log.texts().joinIntoString (" ").contains (failed.reason.c_str()), "real View never exposes unknown path reason");
    }
    if (!pre && face == Face::psr && (width == 300 || width == 900))
    {
        const i18n::ScopedLanguage japanese (i18n::Language::japanese);
        auto failed = frame; failed.status = Status::unavailable; failed.reason = "read_failed";
        view.setMetricFrame (failed); log.clear();
        const auto japaneseImage = render (view);
        check (log.texts().joinIntoString (" ").contains (i18n::tr ("Audio could not be read")), "real View localizes the read failure");
        check (log.texts().contains ("POST " + juce::String::charToString (0x2014)), "Japanese reason keeps Unicode missing value");
        preview (japaneseImage, previewName + "-reason-ja");
    }
    if (pre && face == Face::psr && (width == 300 || width == 900))
    {
        const i18n::ScopedLanguage japanese (i18n::Language::japanese);
        auto unavailable = frame; unavailable.status = Status::unavailable; unavailable.reason = "select_reference_post";
        view.setMetricFrame (unavailable); log.clear();
        const auto hintImage = render (view);
        check (log.texts().joinIntoString (" ").contains (i18n::tr ("Choose Reference in POST")),
               "PRE explains where its Reference is selected");
        preview (hintImage, previewName + "-select-reference-ja");
    }
    view.setMetricFrame (frame);
    view.setMetricFace (Face::lu);
    check (view.luFaceShown() && !view.metricFaceActive() && view.lu().windowControl().isVisible(), "switch metric to LU");
    view.lu().windowControl().onClick();
    check (view.lu().window() == lu_needle::Window::shortTerm, "LU M/S still operates");
    controls (view); render (view);
    view.setMetricFace (Face::vu);
    check (!view.luFaceShown() && !view.metricFaceActive() && !view.lu().windowControl().isVisible(), "switch LU to VU");
    view.setMetricFace (face);
    view.lu().restore (true, lu_needle::Window::integrated);
    check (view.metricFaceSelection() == face && !view.luFaceShown(), "legacy LU restore cannot overwrite metric selection");
    view.lu().setFacts ({});
    check (!view.metricFaceActive() && view.metricFaceSelection() == face, "Reference loss keeps saved face");
    check (differences (plain, render (view), plain.getBounds()) == 0, "Reference loss restores exact VU");
    view.lu().setFacts (reference());
    check (view.metricFaceActive(), "Reference recovery restores metric");
    if (pre && face == Face::sharp)
    {
        view.setPreSharpAvailable (false);
        check (!view.metricFaceActive() && view.metricFaceSelection() == face, "PRE SHARP gate revocation retains choice");
    }
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage currentLanguage (language);
        view.setMetricFace (face);
        if (pre) view.setPreSharpAvailable (false);
        check (view.lu().faceControl().getTooltip() == i18n::tr (pre
                ? "Choose VU / LU / PSR / CREST" : "Choose VU / LU / PSR / CREST / SHARP"),
               "meter face hint matches options in the current role and language");
        if (pre)
        {
            view.setPreSharpAvailable (true);
            check (view.lu().faceControl().getTooltip() == i18n::tr ("Choose VU / LU / PSR / CREST / SHARP"),
                   "PRE tooltip offers SHARP only when the actual menu does");
        }
    }
    check (callbacks == 0, "programmatic face changes remain silent");
}
}
void verifyMetricFaceContract()
{
    verifyChassisSelectionAndStaticCache();
    verifyEmbeddedMidReference();
    const i18n::ScopedLanguage english (i18n::Language::english);
    for (const auto role : {observatory::Role::pre, observatory::Role::post})
        for (const int width : {300,450,600,900})
            for (const auto face : {Face::psr,Face::crest,Face::sharp}) verify (role,width,face);
}
}
#ifdef KIRIN_METRIC_FACE_STANDALONE
int main (int argc, char** argv)
{
    try
    {
        const juce::ScopedJuceInitialiser_GUI initialise;
        if (argc > 1)
        {
           #if JUCE_WINDOWS
            ::_putenv_s ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR",argv[1]);
           #else
            ::setenv ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR",argv[1],1);
           #endif
        }
        hypha::tests::verifyMetricFaceContract();
        std::cout << "Metric real View contracts passed\n"; return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif
