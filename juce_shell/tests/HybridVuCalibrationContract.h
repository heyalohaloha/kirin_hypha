#pragma once

#include "../src/HyphaHybridVuPainter.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaVuCalibrationPreference.h"
#include "../src/HyphaLanguage.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests::vu_calibration_contract
{
inline void require (bool condition, const char* message)
{
    if (! condition) { std::cerr << "VU calibration: " << message << '\n'; std::exit (1); }
}

class FaultStream final
{
public:
    enum class Mode { partial, partialReportedSuccess, flushFailure, alteredContent };
    FaultStream (const juce::File& file, Mode fault) : stream (file, 0), mode (fault) {}
    bool openedOk() const { return stream.openedOk(); }
    bool write (const void* bytes, std::size_t count)
    {
        if (mode == Mode::partial || mode == Mode::partialReportedSuccess)
        {
            if (! stream.write (bytes, juce::jmin (count, std::size_t { 5 }))) return false;
            return mode == Mode::partialReportedSuccess;
        }
        if (mode == Mode::alteredContent)
        {
            juce::MemoryBlock altered (bytes, count);
            static_cast<char*> (altered.getData())[0] = 'X';
            return stream.write (altered.getData(), count);
        }
        return stream.write (bytes, count);
    }
    void flush()
    {
        stream.flush();
        if (mode == Mode::flushFailure) faultStatus = juce::Result::fail ("fixture flush failure");
    }
    const juce::Result& getStatus() const
    { return faultStatus.failed() ? faultStatus : stream.getStatus(); }
    juce::int64 getPosition() { return stream.getPosition(); }
private:
    juce::FileOutputStream stream;
    Mode mode;
    juce::Result faultStatus = juce::Result::ok();
};

inline void verifyPreference()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("hypha-vu-calibration-fixture-" + juce::Uuid().toString());
    require (root.createDirectory().wasOk(), "disposable directory");
    const auto a = vu_calibration::scopeKey ("project-a", "pre-a");
    const auto b = vu_calibration::scopeKey ("project-a", "pre-b");
    const auto otherProject = vu_calibration::scopeKey ("project-b", "pre-a");
    require (a.length() == 64 && a.containsOnly ("0123456789abcdef"), "hashed path");
    require (a != b && a != otherProject, "exact project and PRE isolation");
    const auto prefix63 = juce::String::repeatedString ("a", 63);
    const auto id64a = prefix63 + "b", id64b = prefix63 + "c";
    require (vu_calibration::scopeKey ("project", id64a)
             != vu_calibration::scopeKey ("project", id64b), "legal 64-byte instance tail distinguishes scopes");
    require (vu_calibration::scopeKey (id64a, "instance")
             != vu_calibration::scopeKey (id64b, "instance"), "legal 64-byte project tail distinguishes scopes");
    require (vu_calibration::scopeKey ("ab", "c") != vu_calibration::scopeKey ("a", "bc"),
             "unambiguous identity encoding");
    require (vu_calibration::scopeKey ({}, "pre").isEmpty(), "identity unavailable");
    vu_calibration::Preference pre (root), post (root);
    require (pre.read (a, 0) == -18 && post.read (a, 0) == -18, "missing file default");
    require (pre.set (a, -14, 1), "explicit PRE selection");
    require (post.read (a, 249) == -18 && post.read (a, 250) == -14, "250 ms cache boundary");
    require (post.set (a, -20, 251), "explicit POST selection");
    require (pre.read (a, 251) == -20, "both sides share calibration");
    require (pre.read (b, 252) == -18 && pre.read (otherProject, 253) == -18,
             "different chain and project default");
    require (pre.set (b, -12, 254) && post.read (a, 501) == -20,
             "different chain write does not change selected PRE");
    require (pre.read (a, 502) == -20, "returning to chain reads its setting");
    require (pre.read ({}, 503) == -20, "unavailable locator retains adopted calibration");
    require (pre.read (otherProject, 504) == -18, "new valid missing scope adopts its default");
    require (pre.read ({}, 505) == -18, "unavailable locator keeps adopted new scope default");
    require (pre.read (a, 506) == -20, "new valid existing scope adopts its nondefault");
    vu_calibration::Preference reopened (root);
    require (reopened.read (a, 0) == -20 && reopened.read (b, 1) == -12,
             "reopen persists exact chain");
    require (! reopened.set (a, -13, 2) && reopened.read (a, 3) == -20,
             "invalid choice preserves prior value");
    require (pre.fileFor (a).getFileName() == a + ".txt", "no identity in filename");
    require (pre.fileFor (a).getSize() < 256, "small complete shared record");
    for (const auto malformed : { "", "-14", "KIRIN_HYPHA_VU_CALIBRATION_V2\n", "extra" })
    {
        require (pre.fileFor (a).replaceWithText (malformed), "fixture corruption");
        vu_calibration::Preference damaged (root);
        require (damaged.read (a, 0) == -18, "corrupt file safely defaults");
    }
    require (pre.set (a, -16, 1000), "recover by explicit choice");
    auto text = pre.fileFor (a).loadFileAsString();
    require (pre.fileFor (b).replaceWithText (text), "wrong-owner fixture");
    vu_calibration::Preference wrongOwner (root);
    require (wrongOwner.read (b, 0) == -18, "wrong scope cannot publish calibration");
    require (pre.set (a, -18, std::numeric_limits<juce::uint32>::max() - 100u), "clock wrap setup");
    require (post.set (a, -12, 0), "other side after clock wrap");
    require (pre.read (a, 148) == -18 && pre.read (a, 149) == -12, "cache handles uint32 wrap");
    const auto blocker = root.getChildFile ("blocked");
    require (blocker.replaceWithText ("not a directory"), "write-failure fixture");
    vu_calibration::Preference unavailable (blocker);
    require (unavailable.read (a, 0) == -18 && ! unavailable.set (a, -14, 1)
             && unavailable.read (a, 251) == -18, "failed write retains old setting");
    require (! unavailable.set ({}, -14, 1), "unknown identity cannot write");
    require (pre.set (a, -14, 2000), "old nondefault value before write faults");
    require (post.read (a, 2250) == -14, "other side observes previous selection");
    const auto previous = pre.fileFor (a).loadFileAsString();
    for (const auto mode : { FaultStream::Mode::partial, FaultStream::Mode::partialReportedSuccess,
                            FaultStream::Mode::flushFailure, FaultStream::Mode::alteredContent })
    {
        const bool published = pre.setUsingWriter (a, -20, 2001,
            [mode] (const juce::File& file, const juce::String& nextText)
            {
                return vu_calibration::writeAtomicText (file, nextText, [mode] (const juce::File& temporary)
                    { return std::make_unique<FaultStream> (temporary, mode); });
            });
        require (! published && pre.read (a, 2002) == -14, "failed/partial writer retains cached choice");
        require (pre.fileFor (a).loadFileAsString() == previous && post.read (a, 2500) == -14,
                 "failed/partial writer preserves complete file and other side");
        vu_calibration::Preference afterFailure (root);
        require (afterFailure.read (a, 0) == -14, "reopen after failed/partial writer keeps old choice");
    }
    require (root.deleteRecursively(), "dispose fixtures");
}

inline juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

inline int difference (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area)
{
    int changed = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            changed += a.getPixelAt (x, y).getARGB() != b.getPixelAt (x, y).getARGB();
    return changed;
}

inline void writePreview (const juce::Image& image, observatory::Role role,
                         i18n::Language language, int value, int channels)
{
    const auto directory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_VU_CALIBRATION_PREVIEW_DIR", {});
    if (directory.isEmpty()) return;
    const auto filename = juce::String (role == observatory::Role::pre ? "pre-" : "post-")
        + (language == i18n::Language::japanese ? "ja-" : "en-") + juce::String (image.getWidth())
        + "-" + juce::String (value) + "-" + juce::String (channels) + ".png";
    auto stream = juce::File (directory).getChildFile (filename).createOutputStream();
    require (stream != nullptr && juce::PNGImageFormat().writeImageToStream (image, *stream), "preview PNG");
}

inline void verifyRendering()
{
    for (const auto reference : vu_calibration::choices)
    {
        require (std::abs (hybrid_vu::vuNormalized (reference, reference)
                          - hybrid_vu::vuNormalized (-18)) < 1.0e-6f, "0 VU maps identically");
        require (std::abs (hybrid_vu::vuNormalized (reference + 3, reference) - 1.0f) < 1.0e-6f,
                 "+3 VU dial endpoint");
        require (hybrid_vu::vuNormalized (std::numeric_limits<double>::quiet_NaN(), reference) == 0.0f,
                 "missing channel parks needle at the same floor");
    }
    require (std::abs (hybrid_vu::vuNormalized (-18, -13) - hybrid_vu::vuNormalized (-18)) < 1.0e-6f,
             "invalid default");
    require (std::abs (hybrid_vu::vuNormalized (-18, -20) - 0.933f) < 1.0e-6f,
             "-18 dBFS at reference -20 equals +2 VU");
    require (std::abs (hybrid_vu::vuNormalized (-18, -12) - 0.3745f) < 1.0e-6f,
             "-18 dBFS at reference -12 equals -6 VU");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scopedLanguage (language);
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
            for (const auto preset : observatory::sizePresets)
            {
                observatory::View view (role);
                view.setSize (preset.width, preset.height);
                KirinMeterSession meter {};
                meter.state = KIRIN_METER_SESSION_ACTIVE;
                meter.channels = 2;
                meter.channel_vu_dbfs[0] = meter.channel_vu_dbfs[1] = -18;
                meter.true_peak = -3.0;
                meter.channel_instant_true_peak_dbtp[0] = -12.0;
                meter.channel_instant_true_peak_dbtp[1] = -13.0;
                view.setMeterSnapshot (meter, true);
                view.setManualHybridVuVisible (true);
                auto* control = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-vu-calibration"));
                auto* close = view.findChildWithID ("observatory-hybrid-vu");
                auto* clear = view.findChildWithID ("observatory-clear-peak-clip");
                require (control != nullptr && close != nullptr && clear != nullptr, "three controls");
                require (control->isVisible() && control->getWidth() > 0
                    && view.getLocalBounds().contains (control->getBounds())
                    && ! control->getBounds().intersects (close->getBounds())
                    && ! control->getBounds().intersects (clear->getBounds()), "calibration bounds");
                bool opened = false;
                view.onVuCalibrationMenu = [&opened] { opened = true; };
                control->onClick();
                require (opened, "click opens selection callback");
                const auto baseline = render (view);
                const auto metrics = juce::Rectangle<int> (0, juce::roundToInt (view.getHeight() * 0.72f),
                    view.getWidth(), juce::roundToInt (view.getHeight() * 0.15f));
                for (const auto reference : vu_calibration::choices)
                {
                    view.setVuCalibration (reference);
                    text_style::ShownTextLog shown;
                    const auto image = render (view);
                    require (shown.texts().contains (vu_calibration::label (reference)), "current reference is legible");
                    require (view.vuCalibration() == reference, "one shared L/R reference");
                    require (difference (baseline, image, metrics) == 0, "LUFS TP and CREST pixels unchanged");
                    if (reference != -18) require (difference (baseline, image, image.getBounds()) > 12,
                                                   "only needle calibration and legend respond");
                    if (reference == -18 || reference == -20) writePreview (image, role, language, reference, 2);
                }
                meter.channels = 1;
                view.setMeterSnapshot (meter, true);
                view.setVuCalibration (-18);
                const auto mono = render (view);
                view.setVuCalibration (-20);
                const auto calibratedMono = render (view);
                const auto parkedRight = juce::Rectangle<int> (view.getWidth() / 2,
                    juce::roundToInt (view.getHeight() * 0.30f), view.getWidth() / 2,
                    juce::roundToInt (view.getHeight() * 0.40f));
                require (difference (mono, calibratedMono, parkedRight) == 0, "mono R needle remains parked");
                writePreview (calibratedMono, role, language, -20, 1);
                view.setVuCalibration (-20, false);
                require (! control->isEnabled(), "unknown identity disables selection");
                auto* calibration = dynamic_cast<vu_calibration::Control*> (control);
                require (calibration != nullptr, "calibration control type");
                juce::Image disabled (juce::Image::ARGB, control->getWidth(), control->getHeight(), true);
                juce::Image hovered (juce::Image::ARGB, control->getWidth(), control->getHeight(), true);
                { juce::Graphics g (disabled); calibration->paintButton (g, false, false); }
                { juce::Graphics g (hovered); calibration->paintButton (g, true, true); }
                require (difference (disabled, hovered, disabled.getBounds()) == 0,
                         "disabled calibration never highlights on hover");
                view.setManualHybridVuVisible (false);
                require (! control->isVisible(), "calibration only belongs to VU");
            }
    }
    require (i18n::hasTranslation ("Set shared 0 VU. Audio, LUFS, TP and needle speed stay unchanged."),
             "Japanese help exists");
    require (i18n::hasTranslation ("VU calibration could not be saved. Previous setting retained."),
             "Japanese explicit failure exists");
    require (i18n::hasTranslation ("Chain unavailable; previous VU setting retained."), "Japanese scope unavailable exists");
}

inline void verify()
{
    verifyPreference();
    verifyRendering();
    std::cout << "VU calibration: PASS (5 references; exact shared scope; cache/reopen/corruption/write failure; "
                 "PRE/POST, mono/stereo, five sizes, JA/EN)\n";
}
}
