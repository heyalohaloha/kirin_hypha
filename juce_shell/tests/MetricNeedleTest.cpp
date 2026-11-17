#include "HyphaMetricFacePainter.h"
#include "HyphaVuNeedlePainter.h"
#include "HyphaTruePeakRail.h"
#include "HyphaLanguage.h"
#include <iostream>
#include <stdexcept>
using namespace hypha::metric_needle;
namespace
{
void require (bool value, const char* message) { if (!value) throw std::runtime_error (message); }
void model()
{
    require (normaliseLegacyChoice (4) == 0 && normaliseLegacyChoice (5) == 1, "legacy I maps to M");
    require (normaliseLegacyChoice (2) == 2 && normaliseLegacyChoice (3) == 3, "legacy S retained");
    require (restoredFace (std::nullopt, 4) == Face::vu && restoredFace (std::nullopt, 5) == Face::lu,
             "absent new setting restores legacy face");
    require (restoredFace (4, 0) == Face::sharp && restoredFace (99, 1) == Face::vu,
             "new face independent; unknown value fails to VU");
    for (const auto face : {Face::psr, Face::crest, Face::sharp})
    {
        const auto range = extent (face);
        require (normalized (face, -range) == 0.0f && normalized (face, 0) == 0.5f
            && normalized (face, range) == 1.0f, "symmetric scale endpoints and zero");
        require (normalized (face, range * 2) == 1.0f && normalized (face, -range * 2) == 0.0f,
                 "only needle position is clamped");
        require (!normalized (face, missing) && !normalized (face, std::numeric_limits<double>::infinity()),
                 "missing is not a zero-position needle");
        Frame frame; frame.face = face; frame.status = Status::ready; frame.median = 2;
        frame.own = 2 + range * 2; frame.pre = 2; frame.reference = 2 - range / 2;
        frame.band = {2 - range / 2, 2 + range / 2};
        auto result = reading (frame);
        require (result.own == range * 2 && result.ownPosition == 1.0f, "retain unclamped measurement");
        require (result.prePosition == 0.5f && result.referencePosition == 0.25f, "three independent needles");
        require (result.relativeBand && result.relativeBand->low == -range / 2, "band is relative to Cue zero");
        frame.pre = missing; result = reading (frame);
        require (!result.prePosition && result.ownPosition && result.referencePosition, "missing PRE only hides PRE");
        frame.ownIsPre = true; frame.pre = 1; result = reading (frame);
        require (!result.prePosition && result.ownPosition, "PRE own face never duplicates a paired PRE needle");
        for (const auto state : {Status::measuring, Status::unavailable})
        {
            frame.status = state; result = reading (frame);
            require (!result.ownPosition && !result.prePosition && !result.referencePosition && !result.relativeBand,
                     "unready reference does not manufacture zero or band");
        }
        frame.status = Status::ready; frame.median = missing;
        require (!reading (frame).ownPosition, "missing median cannot become zero");
        frame.median = 2; frame.band = {5, 1};
        require (!reading (frame).relativeBand, "reversed band rejected");
        frame.band = {2 + range * 2, 2 + range * 3};
        require (!reading (frame).relativeBand, "offscale band not painted as false endpoint band");
    }
    require (!normalized (Face::lu, 0) && !normalized (Face::vu, 0), "LU and VU cannot borrow metric scale");
    require (extent (Face::sharp) == 1 && extent (Face::psr) == 9 && extent (Face::crest) == 9,
             "acum and dB extents remain distinct");
}
void render()
{
    for (const auto width : {300, 450, 600, 900})
        for (const auto face : {Face::psr, Face::crest, Face::sharp})
            for (const auto status : {Status::ready, Status::measuring, Status::unavailable})
            {
                juce::Image image (juce::Image::RGB, width, 300, true);
                juce::Graphics graphics (image);
                Frame frame; frame.face = face; frame.status = status; frame.song = "Reference song";
                frame.median = 1; frame.own = 1.2; frame.pre = 0.7; frame.reference = 1.5; frame.band = {0.8, 1.3};
                std::vector<hypha::metric_face::TextBox> trace;
                hypha::metric_face::paint (graphics, {0,0,width,300}, frame,
                                           hypha::presentation::defaultContext(), &trace);
                require (!trace.empty(), "render exposes text contracts");
                const std::vector<juce::String> expectedTicks = face == Face::sharp
                    ? std::vector<juce::String> {"-1", "-0.5", "0", "+0.5", "+1"}
                    : std::vector<juce::String> {"-9", "-6", "-3", "0", "+3", "+6", "+9"};
                // The heading is followed by every scale label, in dial order.
                require (trace.size() >= expectedTicks.size() + 2, "all scale ticks recorded");
                for (std::size_t tick = 0; tick < expectedTicks.size(); ++tick)
                    require (trace[tick + 1].text == expectedTicks[tick], "exact face-specific scale labels");
                require (trace[expectedTicks.size() + 1].text.startsWith ("POST "),
                         "no additional or old half-range scale labels");
                for (const auto& box : trace)
                {
                    require (box.fontHeight >= 12.0f, "minimum twelve pixel typography");
                    require (juce::Rectangle<float> (0,0,static_cast<float> (width),300).contains (box.area),
                             "all label rectangles remain within face");
                }
                for (const auto position : {0.0f,0.5f,1.0f})
                    require (juce::Rectangle<float> (0,0,static_cast<float> (width),300).contains (
                        hypha::metric_face::needlePoint ({0,0,width,300}, position, 1.06f)), "needle within single dial");
            }
}
// Mirror the existing VU chrome bounds, including its title and lit CLIP lamps.
std::vector<juce::Rectangle<float>> chromeAreas (juce::Rectangle<float> face)
{
    using hypha::hybrid_vu::peak_rail::scaled;
    const auto width = face.getWidth();
    std::vector<juce::Rectangle<float>> result {{face.getCentreX() - width * .16f,
        face.getY() + face.getHeight() * .005f, width * .32f, face.getHeight() * .11f}};
    const auto y = face.getY() + face.getHeight() * .20f;
    const auto radius = scaled (8.0f, width, 2.6f);
    for (const auto side : {-1.0f, 1.0f})
        result.push_back ({face.getCentreX() + side * scaled (36.0f, width, 12.0f) - radius,
                           y - radius, radius * 2, radius * 2});
    if (width >= 410.0f)
        result.push_back ({face.getCentreX() - scaled (29.0f,width), y - scaled (11.0f,width),
                           scaled (58.0f,width), scaled (22.0f,width)});
    return result;
}
void reasons()
{
    const std::array<const char*, 10> codes {"preparing", "unsupported_format", "read_failed", "source_changed",
        "private_copy_required", "capacity_exceeded", "audio_too_short", "cue_too_short", "no_valid_value", "analysis_failed"};
    require (juce::String (reasonText ("/private/user/music.wav")) == "UNAVAILABLE", "unknown reason never exposes a path");
    for (const auto language : {hypha::i18n::Language::english, hypha::i18n::Language::japanese})
    {
        const hypha::i18n::ScopedLanguage scoped (language);
        for (const auto* code : codes)
            for (const int width : {300,450,600,900})
            {
                const int height = width * 2 / 3;
                const auto bounds = hypha::hybrid_vu::needle_painter::faceBounds ({0,0,width,height}).toNearestInt();
                Frame frame; frame.face = Face::crest; frame.status = Status::unavailable;
                frame.reason = code; frame.song = "A long song name must never displace the reason for missing measurement";
                juce::Image image (juce::Image::RGB,width,height,true); juce::Graphics graphics (image);
                std::vector<hypha::metric_face::TextBox> trace;
                hypha::metric_face::paint (graphics,bounds,frame,hypha::presentation::forEditor (width,height),&trace,true);
                require (hypha::i18n::hasTranslation (reasonText (code)), "reason has Japanese catalog entry");
                const auto expected = hypha::i18n::tr (reasonText (code));
                bool found = false;
                for (const auto& box : trace)
                {
                    require (box.fontHeight >= 12 && bounds.toFloat().contains (box.area), "reason font/bounds unchanged");
                    require (!box.text.contains (code), "internal reason code is never visible");
                    if (box.text.contains (expected))
                    {
                        found = true;
                        require (!box.text.contains (juce::String::fromUTF8 (frame.song.c_str())), "reason has priority over song");
                        auto context = hypha::presentation::forEditor (width,height); context.referenceCompact = true;
                        require (hypha::text_style::shownWidth (hypha::labelFont (context,hypha::typography::TextRole::legend,
                            hypha::typography::Composition::instrument),box.text,hypha::text_style::LabelPolicy::fixed) <= box.area.getWidth(),
                            "complete reason fits without ellipsis");
                    }
                }
                require (found, "specific missing reason is displayed");
            }
    }
}
void integratedGeometry()
{
    for (const auto width : {300,450,600,900})
        for (const auto metric : {Face::psr, Face::crest, Face::sharp})
        for (const auto status : {Status::ready, Status::measuring, Status::unavailable})
        for (const auto language : {hypha::i18n::Language::english, hypha::i18n::Language::japanese})
        {
            hypha::i18n::setCurrent (language);
            require (hypha::i18n::hasTranslation ("MEASURING") && hypha::i18n::hasTranslation ("UNAVAILABLE"),
                     "both status words have Japanese catalog entries");
            const int height = width * 2 / 3;
            const auto face = hypha::hybrid_vu::needle_painter::faceBounds ({0,0,width,height});
            const auto bounds = face.toNearestInt();
            juce::Image image (juce::Image::RGB, width, height, true);
            juce::Graphics graphics (image);
            auto context = hypha::presentation::forEditor (width,height);
            Frame frame; frame.face = metric; frame.status = status;
            frame.median = 1; frame.own = 1; frame.pre = 1.1; frame.reference = 0.9; frame.song = u8"Reference 曲名が長い場合も状態語を翻訳してから曲名を保つ / A very long user supplied song title 2026 version 123";
            std::vector<hypha::metric_face::TextBox> trace;
            hypha::metric_face::paint (graphics, bounds, frame, context, &trace, true);
            if (status != Status::ready)
                require (std::any_of (trace.begin(), trace.end(), [&] (const auto& box) { return box.text.contains (hypha::i18n::tr (status == Status::measuring ? "MEASURING" : "UNAVAILABLE")); }),
                         "status is translated before joining the unchanged song title");
            require (trace.back().text.contains (juce::String::fromUTF8 (frame.song.c_str())), "song name is never translated");
            const auto chrome = chromeAreas (face);
            const auto describe = [&] (const char* problem, const juce::String& text, juce::Rectangle<float> area,
                                      int position, int step, juce::Point<float> point) {
                return std::string (problem) + " size=" + std::to_string (width) + "x" + std::to_string (height)
                    + " face=" + label (metric) + " status=" + std::to_string (static_cast<int> (status))
                    + " language=" + std::to_string (static_cast<int> (language)) + " text=" + text.toStdString()
                    + " rect=" + area.toString().toStdString() + " position=" + std::to_string (position)
                    + "/100 reach=" + std::to_string (step) + "/100 point=" + point.toString().toStdString();
            };
            const std::size_t tickCount = metric == Face::sharp ? 5 : 7;
            require (trace.size() > tickCount, "integrated face exposes all tick labels");
            for (std::size_t i = 0; i < trace.size(); ++i)
            {
                require (trace[i].fontHeight >= 12 && bounds.toFloat().contains (trace[i].area), "real face text bounds and font floor");
                for (std::size_t j = i + 1; j < trace.size(); ++j)
                    if (trace[i].area.intersects (trace[j].area))
                        throw std::runtime_error (describe ("labels overlap", trace[i].text + " / " + trace[j].text, trace[i].area, -1, -1, {}));
                for (const auto& reserved : chrome)
                    if (trace[i].area.intersects (reserved))
                        throw std::runtime_error (describe ("text overlaps TP/CLIP", trace[i].text, trace[i].area, -1, -1, {}));
                for (int channel = 0; channel < 2; ++channel)
                    for (const auto& peak : hypha::hybrid_vu::peak_rail::labels (face, channel, context))
                        if (trace[i].area.intersects (peak.area))
                            throw std::runtime_error (describe ("text overlaps TP label", trace[i].text + " / " + peak.text, trace[i].area, -1, -1, {}));
            }
            for (int positionIndex = 0; positionIndex <= 100; ++positionIndex)
                for (int step = 0; step <= 106; ++step)
                {
                    const auto point = hypha::metric_face::needlePoint (bounds, static_cast<float> (positionIndex) / 100.0f, static_cast<float> (step) / 100.0f, true);
                    if (!bounds.toFloat().reduced (2.0f).contains (point))
                        throw std::runtime_error (describe ("needle glow outside face", "", bounds.toFloat(), positionIndex, step, point));
                    for (const auto& reserved : chrome)
                        if (reserved.expanded (2.0f).contains (point))
                            throw std::runtime_error (describe ("needle glow overlaps TP/CLIP", "", reserved, positionIndex, step, point));
                    for (const auto& box : trace)
                        if (box.area.expanded (2.0f).contains (point))
                            throw std::runtime_error (describe ("needle glow overlaps label", box.text, box.area, positionIndex, step, point));
                }
        }
}

}
int main()
{
    try { const juce::ScopedJuceInitialiser_GUI initialise; model(); render(); integratedGeometry(); reasons(); hypha::i18n::setCurrent (hypha::i18n::Language::english); std::cout << "Metric needle and single-face contracts passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
