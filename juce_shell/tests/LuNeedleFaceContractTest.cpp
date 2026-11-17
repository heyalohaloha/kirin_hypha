#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

// The drawn LU face of the Hybrid VU, in PRE and POST at every editor size.
namespace hypha::tests
{
namespace
{
using lu_needle::Facts;
using lu_needle::SamePlace;
using lu_needle::Window;
using lu_needle::Zero;

void require (bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "LU face contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}
#define KIRIN_LU_FACE_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterSession meterFixture()
{
    KirinMeterSession meter {};
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.channels = 2;
    meter.sample_rate = 48000;
    meter.lufs_m = -12.5;
    meter.lufs_s = -13.5;
    meter.lufs_i = -14.5;
    meter.true_peak = -3.2;
    meter.max_true_peak = -1.2;
    meter.channel_vu_dbfs[0] = -28.0;
    meter.channel_vu_dbfs[1] = -27.0;
    meter.channel_instant_true_peak_dbtp[0] = -18.0;
    meter.channel_instant_true_peak_dbtp[1] = -17.0;
    meter.channel_max_true_peak_dbtp[0] = -3.2;
    meter.channel_max_true_peak_dbtp[1] = -4.7;
    return meter;
}

KirinDelta deltaFixture()
{
    KirinDelta delta {};
    delta.mode = KIRIN_DELTA_MODE_ACTIVE;
    delta.lufs = 1.2;
    delta.lufs_s = 0.8;
    return delta;
}

Facts readyFacts (double zeroLufs)
{
    Facts facts;
    facts.readable = true;
    facts.zero = Zero::ready;
    facts.song = "North Road";
    facts.zeroLufs = zeroLufs;
    return facts;
}

juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right)
{
    int count = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}

// The brightest pixel of the 3 x 3 around a point: a needle drawn there raises it.
int brightness (const juce::Image& image, juce::Point<float> point)
{
    int best = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const auto pixel = image.getPixelAt (juce::roundToInt (point.x) + dx, juce::roundToInt (point.y) + dy);
            best = juce::jmax (best, pixel.getRed() + pixel.getGreen() + pixel.getBlue());
        }
    return best;
}

bool needleAt (const juce::Image& with, const juce::Image& without, juce::Rectangle<int> bounds, double lu)
{
    const auto point = hybrid_vu::lu_face::needlePoint (bounds, lu_needle::normalized (lu), 0.97f);
    return brightness (with, point) > brightness (without, point) + 50;
}

bool shows (const observatory::View& view, const juce::String& text)
{
    for (const auto& box : const_cast<observatory::View&> (view).lu().textForTest())
        if (box.text == text) return true;
    return false;
}

bool showsStarting (const observatory::View& view, const juce::String& start)
{
    for (const auto& box : const_cast<observatory::View&> (view).lu().textForTest())
        if (box.text.startsWith (start)) return true;
    return false;
}

void verifyText (observatory::View& view)
{
    const auto& boxes = view.lu().textForTest();
    KIRIN_LU_FACE_REQUIRE (! boxes.empty());
    for (std::size_t index = 0; index < boxes.size(); ++index)
    {
        KIRIN_LU_FACE_REQUIRE (boxes[index].fontHeight >= 12.0f);
        KIRIN_LU_FACE_REQUIRE (view.getLocalBounds().toFloat().contains (boxes[index].area));
        for (std::size_t other = index + 1; other < boxes.size(); ++other)
            if (boxes[index].area.intersects (boxes[other].area))
            {
                std::cerr << view.getWidth() << ": \"" << boxes[index].text << "\" meets \"" << boxes[other].text << "\"\n";
                KIRIN_LU_FACE_REQUIRE (! boxes[index].area.intersects (boxes[other].area));
            }
    }
}

void writePreview (const juce::Image& image, observatory::Role role, const char* state, int width)
{
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    if (directory.isEmpty() || (width != 300 && width != 600 && width != 900)) return;
    auto output = juce::File (directory).getChildFile (juce::String (role == observatory::Role::pre ? "pre" : "post")
        + "-lu-" + state + "-" + juce::String (width) + ".png").createOutputStream();
    KIRIN_LU_FACE_REQUIRE (output != nullptr && juce::PNGImageFormat().writeImageToStream (image, *output));
}

void prepare (observatory::View& view, observatory::SizePreset preset, bool post)
{
    view.setSize (preset.width, preset.height);
    view.setConnection (post ? "PAIR MIX" : "SOURCE PRE", COL_LED_BLUE,
                        post ? observatory::ConnectionState::paired : observatory::ConnectionState::source);
    view.setMeterSnapshot (meterFixture(), true);
    view.setManualHybridVuVisible (true);
}

void verifyFace (observatory::Role role, observatory::SizePreset preset)
{
    const bool post = role == observatory::Role::post;
    const bool compact = preset.width * 0.964f < 410.0f;
    observatory::View plain (role), view (role);
    prepare (plain, preset, post);
    prepare (view, preset, post);
    const auto bounds = view.getLocalBounds();
    const auto vuImage = render (plain);
    writePreview (vuImage, role, "off-vu", preset.width);
    auto& face = view.lu().faceControl();
    auto& window = view.lu().windowControl();
    auto* calibration = view.findChildWithID ("observatory-vu-calibration");
    KIRIN_LU_FACE_REQUIRE (calibration != nullptr && view.findChildWithID ("observatory-lu-face") == &face);

    // Kirin OS's Reference cannot be read: the VU as it was, no LU control, a saved choice kept.
    KIRIN_LU_FACE_REQUIRE (! face.isVisible() && ! window.isVisible() && ! view.luFaceShown());
    view.lu().restore (true, Window::shortTerm);
    KIRIN_LU_FACE_REQUIRE (view.lu().selected() && ! view.lu().active() && ! face.isVisible());
    KIRIN_LU_FACE_REQUIRE (differentPixels (vuImage, render (view)) == 0);

    // Readable, nothing chosen: the needle rests, the face says where to choose a song.
    int choices = 0; bool chosenFace = false; auto chosenWindow = Window::momentary;
    view.lu().onChoice = [&] (bool luFace, Window next) { ++choices; chosenFace = luFace; chosenWindow = next; };
    Facts facts;
    facts.readable = true;
    view.lu().setFacts (facts);
    KIRIN_LU_FACE_REQUIRE (view.luFaceShown() && face.isVisible() && window.isVisible() && ! calibration->isVisible());
    for (auto* control : { &face, &window })
    {
        KIRIN_LU_FACE_REQUIRE (bounds.contains (control->getBounds()));
        for (const char* id : { "observatory-hybrid-vu", "observatory-clear-peak-clip" })
            KIRIN_LU_FACE_REQUIRE (! control->getBounds().intersects (view.findChildWithID (id)->getBounds()));
    }
    KIRIN_LU_FACE_REQUIRE (! face.getBounds().intersects (window.getBounds()));
    KIRIN_LU_FACE_REQUIRE (view.lu().controlFontHeightForTest() >= 12.0f);
    const auto chooseImage = render (view);
    writePreview (chooseImage, role, "choose-song", preset.width);
    KIRIN_LU_FACE_REQUIRE (differentPixels (vuImage, chooseImage) > 200);
    KIRIN_LU_FACE_REQUIRE (shows (view, post ? "CHOOSE A SONG IN B OR C" : "CHOOSE A SONG IN POST REFERENCE"));
    KIRIN_LU_FACE_REQUIRE (shows (view, "S ---") && shows (view, "0") && ! showsStarting (view, "0 ="));
    verifyText (view);
    {
        const i18n::ScopedLanguage japanese (i18n::Language::japanese);
        text_style::ShownTextLog log;
        render (view);
        KIRIN_LU_FACE_REQUIRE (log.texts().joinIntoString ({}).contains (juce::String::fromUTF8 (
            post ? u8"BかCで曲を選ぶ" : u8"POSTのReferenceで曲を選ぶ")));
        verifyText (view);
    }

    // A song without a value from Kirin OS: its name and the reason, the needle still at rest.
    facts.zero = Zero::noValue;
    facts.song = "East Field";
    view.lu().setFacts (facts);
    render (view);
    KIRIN_LU_FACE_REQUIRE (shows (view, "0 = East Field") && shows (view, "NOT MEASURED IN KIRIN OS") && shows (view, "S ---"));
    verifyText (view);

    // The song's loudness is the 0: the needle is at (LUFS - 0), and a different 0 moves it.
    view.lu().setFacts (readyFacts (-4.0)); // S -13.5 reads -9.5
    const auto lowImage = render (view);
    KIRIN_LU_FACE_REQUIRE (shows (view, "0 = North Road") && shows (view, "-4.0 LUFS") && shows (view, "S -9.5"));
    verifyText (view);
    view.lu().setFacts (readyFacts (-18.0)); // the same loudness reads +4.5
    const auto highImage = render (view);
    KIRIN_LU_FACE_REQUIRE (shows (view, "-18.0 LUFS") && shows (view, "S +4.5"));
    KIRIN_LU_FACE_REQUIRE (needleAt (lowImage, highImage, bounds, -9.5) && needleAt (highImage, lowImage, bounds, 4.5));
    view.lu().setFacts (readyFacts (20.0)); // far below the scale: the left end, the true figure in the readout
    KIRIN_LU_FACE_REQUIRE (needleAt (render (view), highImage, bounds, -18.0) && shows (view, "S -33.5"));

    // M, S and I, chosen with the window control and reported for saving.
    view.lu().setFacts (readyFacts (-4.0));
    window.onClick();
    KIRIN_LU_FACE_REQUIRE (choices == 1 && chosenFace && chosenWindow == Window::integrated && window.getButtonText() == "I");
    const auto integratedImage = render (view);
    KIRIN_LU_FACE_REQUIRE (shows (view, "I -10.5") && needleAt (integratedImage, highImage, bounds, -10.5));
    window.onClick();
    KIRIN_LU_FACE_REQUIRE (choices == 2 && chosenWindow == Window::momentary && window.getButtonText() == "M");
    const auto momentaryImage = render (view);
    KIRIN_LU_FACE_REQUIRE (shows (view, "M -8.5") && needleAt (momentaryImage, integratedImage, bounds, -8.5));

    // The PRE needle: only in POST, only with a confirmed PRE of the same moment.
    KIRIN_LU_FACE_REQUIRE (! showsStarting (view, "POST - PRE") && ! shows (view, "PRE"));
    view.setDeltaSnapshot (deltaFixture(), true);
    const auto pairedImage = render (view);
    KIRIN_LU_FACE_REQUIRE (showsStarting (view, "POST - PRE") == post);
    if (post)
    {
        KIRIN_LU_FACE_REQUIRE (shows (view, "POST - PRE  +1.2 LU") && needleAt (pairedImage, momentaryImage, bounds, -9.7));
        KIRIN_LU_FACE_REQUIRE (compact || (shows (view, "POST") && shows (view, "PRE")));
        verifyText (view);
        writePreview (pairedImage, role, "zero-pre", preset.width);
        view.setDeltaSnapshot (deltaFixture(), false);
        KIRIN_LU_FACE_REQUIRE (differentPixels (render (view), momentaryImage) == 0);
        view.setDeltaSnapshot (deltaFixture(), true);
    }
    else
        KIRIN_LU_FACE_REQUIRE (differentPixels (pairedImage, momentaryImage) == 0);

    // The set's band, p10 to p90, behind the needles.
    auto banded = readyFacts (-4.0);
    banded.band = { -7.0, -1.0 };
    view.lu().setFacts (banded);
    const auto bandImage = render (view);
    writePreview (bandImage, role, "zero-band", preset.width);
    const auto bandPoint = hybrid_vu::lu_face::needlePoint (bounds, lu_needle::normalized (1.5), 0.925f);
    KIRIN_LU_FACE_REQUIRE (bandImage.getPixelAt (juce::roundToInt (bandPoint.x), juce::roundToInt (bandPoint.y))
                           != pairedImage.getPixelAt (juce::roundToInt (bandPoint.x), juce::roundToInt (bandPoint.y)));
    verifyText (view);

    // The song at the same place (FOLLOW A): a third needle; where Kirin OS has no value, a short reason.
    auto following = banded;
    following.samePlace = SamePlace::shown;
    following.samePlaceLufs = 1.5; // +5.5 on the face
    view.lu().setFacts (following);
    const auto followImage = render (view);
    writePreview (followImage, role, "follow-a", preset.width);
    KIRIN_LU_FACE_REQUIRE (needleAt (followImage, bandImage, bounds, 5.5));
    KIRIN_LU_FACE_REQUIRE (compact || shows (view, "REF"));
    verifyText (view);
    following.samePlace = SamePlace::noValueHere;
    following.samePlaceLufs = lu_needle::nan;
    view.lu().setFacts (following);
    KIRIN_LU_FACE_REQUIRE (differentPixels (render (view), followImage) > 0 && ! shows (view, "REF"));
    KIRIN_LU_FACE_REQUIRE (shows (view, "REF: NO VALUE HERE") || compact);
    verifyText (view);

    // Back to the VU with the same control; Reference going away also returns the VU and keeps the choice.
    face.onClick();
    KIRIN_LU_FACE_REQUIRE (choices == 3 && ! chosenFace && ! view.luFaceShown() && face.isVisible() && ! window.isVisible());
    KIRIN_LU_FACE_REQUIRE (calibration->isVisible());
    face.onClick();
    KIRIN_LU_FACE_REQUIRE (choices == 4 && chosenFace && view.luFaceShown());
    view.lu().setFacts (Facts {});
    KIRIN_LU_FACE_REQUIRE (! view.luFaceShown() && view.lu().selected() && ! face.isVisible() && calibration->isVisible());
    view.setDeltaSnapshot (deltaFixture(), false);
    KIRIN_LU_FACE_REQUIRE (differentPixels (vuImage, render (view)) == 0);
}
}

void verifyLuNeedleFace()
{
    for (const auto role : { observatory::Role::pre, observatory::Role::post })
        for (const auto preset : observatory::sizePresets)
            verifyFace (role, preset);
}
}
