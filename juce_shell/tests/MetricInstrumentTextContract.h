#pragma once

// The words of the PSR/CREST/SHARP faces against the photographed instrument: every zero, song, reason,
// headline and figure keeps off the gold rim, the hubs, the arcs, the ticks and one another, at every
// editor size and magnification. Read from what the real View draws, not from the layout itself.
namespace metric_text_contract
{
inline void verifyEmbeddedMidTypography()
{
    auto style = presentation::forEditor (300, 200); style.referenceCompact = true;
    constexpr auto family = typography::Composition::instrument;
    juce::Image image (juce::Image::RGB, 300, 200, true); juce::Graphics g (image);
    for (const auto role : {typography::TextRole::axis, typography::TextRole::legend})
    {
        g.setFont (labelFont (presentation::forEditor (300, 200), typography::TextRole::unit, family));
        check (g.getCurrentFont().getHeight() < 12.0f, "the initial unit font must be smaller than the required role");
        metric_face::drawCandidateText (g, "+24", {10, 10, 60, 18}, style, role, COL_NORMAL);
        check (g.getCurrentFont().getHeight() == labelFont (style, role, family).getHeight()
               && g.getCurrentFont().getHeight() >= 12.0f, "axis and legend explicitly replace the prior unit font");
    }
}

struct Scene
{
    observatory::Role role;
    Face face;
    Status status;
    juce::String name, song;
    const char* reason = "";
};

// Asset rows and columns of both 1536 x 1024 photographs, measured on the pictures.
inline float inset (float row)
{
    struct Point { float row, x; };
    constexpr Point edge[] { {540, 60}, {564, 64}, {596, 78}, {628, 90}, {644, 106}, {660, 127}, {676, 160}, {686, 190} };
    if (row <= edge[0].row) return edge[0].x;
    for (std::size_t i = 1; i < std::size (edge); ++i)
        if (row <= edge[i].row) return juce::jmap (row, edge[i - 1].row, edge[i].row, edge[i - 1].x, edge[i].x);
    return edge[std::size (edge) - 1].x;
}

inline juce::Font wordFont (int width, int height, typography::TextRole role)
{
    auto style = presentation::forEditor (width, height);
    style.referenceCompact = true;
    return labelFont (style, role, typography::Composition::instrument);
}

// The ink of a centred word: its drawn area shrunk to the text itself.
inline juce::Rectangle<float> ink (const juce::String& text, juce::Rectangle<float> area, const juce::Font& font)
{
    const auto shaped = requiresNativeTextFont (text) ? nativeTextFontLike (font) : font;
    const auto width = shaped.getStringWidthFloat (text);
    if (width > area.getWidth() + 0.5f) return area; // wrapped over the whole area
    return juce::Rectangle<float> (width, font.getHeight()).withCentre (area.getCentre());
}

inline bool crosses (juce::Rectangle<float> box, juce::Line<float> line, float halfStroke)
{
    const auto grown = box.expanded (halfStroke);
    const auto steps = juce::jmax (8, juce::roundToInt (line.getLength() * 2.0f));
    for (int i = 0; i <= steps; ++i)
        if (grown.contains (line.getPointAlongLineProportionally (static_cast<float> (i) / static_cast<float> (steps)))) return true;
    return false;
}

inline bool overlapsCircle (juce::Rectangle<float> box, juce::Point<float> centre, float radius)
{ return box.getConstrainedPoint (centre).getDistanceFrom (centre) < radius; }

inline Frame frameFor (const Scene& scene)
{
    Frame frame; frame.face = scene.face; frame.status = scene.status;
    frame.ownIsPre = scene.role == observatory::Role::pre;
    frame.median = scene.face == Face::sharp ? 1.6 : 12.34;
    frame.own = frame.median + metric_needle::extent (scene.face) * 0.4;
    frame.pre = frame.median - metric_needle::extent (scene.face) * 0.4;
    frame.song = scene.song.toStdString(); frame.reason = scene.reason;
    return frame;
}

inline void prepare (observatory::View& view, observatory::Role role, int width, Face face)
{
    const bool pre = role == observatory::Role::pre;
    view.setSize (width, width * 2 / 3);
    view.setConnection (pre ? "SOURCE MIX A" : "PAIR MIX A", COL_LED_BLUE,
                        pre ? observatory::ConnectionState::source : observatory::ConnectionState::paired);
    view.setMeterSnapshot (meter(), true); view.setManualHybridVuVisible (true);
    view.lu().setFacts (reference());
    if (pre && face == Face::sharp) view.setPreSharpAvailable (true);
    view.setMetricFace (face);
    check (view.metricFaceActive() || ! metric_needle::metric (face), "the real metric face is shown");
}

inline juce::Image renderAt (observatory::View& view, float scale)
{
    juce::Image image (juce::Image::RGB, juce::roundToInt (static_cast<float> (view.getWidth()) * scale),
                       juce::roundToInt (static_cast<float> (view.getHeight()) * scale), true);
    juce::Graphics g (image); g.fillAll (juce::Colours::black);
    g.addTransform (juce::AffineTransform::scale (scale));
    view.paintEntireComponent (g, true);
    return image;
}

// Where each word of the scene was drawn.
struct Word { juce::String text; juce::Rectangle<float> box; bool detail; };
inline std::vector<Word> wordsOf (const text_style::ShownTextLog& log, const Scene& scene, int width, int height)
{
    std::vector<Word> words;
    const auto legend = wordFont (width, height, typography::TextRole::legend);
    const auto axis = wordFont (width, height, typography::TextRole::axis);
    const auto headline = i18n::tr (scene.status == Status::measuring ? "PREPARING" : scene.status == Status::unavailable
                                    ? "UNAVAILABLE" : metric_needle::label (scene.face));
    const auto reason = *scene.reason != 0 ? text_style::shownText (metric_needle::reasonText (scene.reason)) : juce::String();
    const auto range = juce::String (metric_needle::extent (scene.face), scene.face == Face::sharp ? 1 : 0);
    for (int i = 0; i < log.texts().size(); ++i)
    {
        const auto text = log.texts()[i];
        const auto area = log.areas()[static_cast<std::size_t> (i)];
        const bool figure = text.startsWith ("PRE ") || text.startsWith ("REF ") || text.startsWith ("POST ");
        const bool song = scene.song.isNotEmpty() && text.isNotEmpty()
            && (text == scene.song || (text.containsChar (0x2026) && scene.song.startsWith (text.upToFirstOccurrenceOf (juce::String::charToString (0x2026), false, false))));
        const bool zero = text.startsWith ("0 = ");
        // A control logs its own text in its own coordinates and the readouts below the dial name CREST too:
        // only the headline on the dial is judged here.
        const bool onDial = area.getCentreY() > static_cast<float> (height) * 0.2f && area.getCentreY() < static_cast<float> (height) * 0.7f;
        const bool title = onDial && text == headline;
        const bool tick = text == "0" || text == "+" + range || text == "-" + range;
        // A reason may take two rows: each row is judged on its own.
        const bool part = reason.isNotEmpty() && text.length() > 2 && reason.contains (text) && onDial && ! figure;
        if (figure || song || zero || title || part)
            words.push_back ({ text, ink (text, area, legend), ! figure });
        else if (tick)
            words.push_back ({ text, ink (text, area, axis), false });
    }
    return words;
}

inline void verifyScene (const Scene& scene, int width, const std::shared_ptr<const metric_face::MetricChassisPair>& bank)
{
    namespace mf = metric_face;
    const int height = width * 2 / 3;
    observatory::View view (scene.role, bank);
    prepare (view, scene.role, width, scene.face);
    view.setMetricFrame (frameFor (scene));
    text_style::ShownTextLog log;
    renderAt (view, 1.0f);
    const auto words = wordsOf (log, scene, width, height);
    context = scene.name + " " + juce::String (width);
    const auto has = [&words] (auto&& match) {
        for (const auto& word : words) if (match (word)) return true;
        return false;
    };
    check (has ([] (const Word& word) { return word.text.startsWith ("REF "); }), "the REF figure is drawn");
    if (scene.status == Status::ready)
        check (has ([] (const Word& word) { return word.text.startsWith ("0 = "); }), "the zero is drawn");
    if (*scene.reason != 0)
    {
        juce::String told;
        for (const auto& word : words)
            if (word.detail && text_style::shownText (metric_needle::reasonText (scene.reason)).contains (word.text)) told << word.text.removeCharacters (" ");
        check (told == text_style::shownText (metric_needle::reasonText (scene.reason)).removeCharacters (" "), "the whole reason is told");
    }
    const auto chassis = mf::pre_preview::chassisBounds (view.getLocalBounds());
    const auto assetX = [&chassis] (float pixel) { return (pixel - static_cast<float> (chassis.getX())) * 1536.0f / static_cast<float> (chassis.getWidth()); };
    const auto assetY = [&chassis] (float pixel) { return (pixel - static_cast<float> (chassis.getY())) * 1024.0f / static_cast<float> (chassis.getHeight()); };
    const auto unit = static_cast<float> (chassis.getWidth()) / 1536.0f;
    for (const auto& word : words)
    {
        context = scene.name + " " + juce::String (width) + " \"" + word.text + "\" " + word.box.toString();
        check (view.getLocalBounds().toFloat().contains (word.box), "a word stays inside the window");
        if (! word.detail)
        {
            // The figures at the two ends of the scale: their digits keep off every tick.
            if (! word.text.startsWithChar ('+') && ! word.text.startsWithChar ('-')) continue;
            const auto digits = word.box.withSizeKeepingCentre (word.box.getWidth(), word.box.getHeight() * 0.72f);
            for (int slot = 0; slot < mf::dialCount (scene.role); ++slot)
            {
                const auto dial = mf::candidateDial (chassis, *bank, scene.role, slot);
                const int groups = scene.face == Face::sharp ? 6 : 8, sub = width <= 300 ? 2 : 3;
                for (int step = 0; step <= groups * sub; ++step)
                {
                    const auto p = mf::candidatePoint (dial, static_cast<float> (step) / static_cast<float> (groups * sub));
                    const bool end = step == 0 || step == groups * sub;
                    const auto reach = end ? mf::endTickReach (dial, scene.face, step != 0, presentation::forEditor (width, height))
                                           : step % sub == 0 ? 1.13f : 1.06f;
                    check (! end || reach > 1.035f, "an end tick is still seen");
                    check (! crosses (digits, { p, dial.pivot + (p - dial.pivot) * reach }, 0.5f), "an end figure stays off the ticks");
                }
            }
            continue;
        }
        // The gold rim: its inner edge at the word's lowest row, and the lip under the flat part.
        check (assetY (word.box.getBottom()) <= 686.0f, "a word stays above the gold rim");
        const auto edge = inset (assetY (word.box.getBottom()));
        check (assetX (word.box.getX()) >= edge && assetX (word.box.getRight()) <= 1536.0f - edge, "a word stays inside the curved rim");
        for (int slot = 0; slot < mf::dialCount (scene.role); ++slot)
        {
            const auto dial = mf::candidateDial (chassis, *bank, scene.role, slot);
            check (! overlapsCircle (word.box, dial.pivot, 70.0f * unit), "a word stays off the hub");
            for (int step = 0; step < 60; ++step)
                check (! crosses (word.box, { mf::candidatePoint (dial, static_cast<float> (step) / 60.0f),
                                              mf::candidatePoint (dial, static_cast<float> (step + 1) / 60.0f) }, 1.0f), "a word stays off the arc");
            const int groups = scene.face == Face::sharp ? 6 : 8, sub = width <= 300 ? 2 : 3;
            for (int step = 0; step <= groups * sub; ++step)
            {
                const auto p = mf::candidatePoint (dial, static_cast<float> (step) / static_cast<float> (groups * sub));
                const auto reach = step * 2 == groups * sub ? 1.16f : step % sub == 0 ? 1.13f : 1.06f;
                check (! crosses (word.box, { p, dial.pivot + (p - dial.pivot) * reach }, 1.0f), "a word stays off the ticks");
            }
            if (scene.status == Status::ready)
                for (int turn = 0; turn <= 100; ++turn)
                    check (! crosses (word.box, { dial.pivot, dial.pivot + (mf::candidatePoint (dial, static_cast<float> (turn) / 100.0f) - dial.pivot) * 1.11f }, 1.0f),
                           "a word stays off the whole needle sweep");
        }
    }
    for (std::size_t i = 0; i < words.size(); ++i)
        for (std::size_t j = i + 1; j < words.size(); ++j)
        {
            if (! words[i].detail && ! words[j].detail) continue;
            context = scene.name + " " + juce::String (width) + " \"" + words[i].text + "\" / \"" + words[j].text + "\"";
            check (! words[i].box.intersects (words[j].box), "two words never share a place");
        }
    const auto legend = wordFont (width, height, typography::TextRole::legend);
    check (legend.getHeight() >= 12.0f, "words are 12 px or more");
    // A long song is cut with an ellipsis, not by the edge of its box.
    if (scene.song.length() > 60 && scene.status == Status::ready)
        check (has ([] (const Word& word) { return word.detail && word.text.containsChar (0x2026); }), "a long song is cut with an ellipsis");
}

// Every pixel the zero and the song paint is on the glass: above the gold rim and inside its curved corners.
inline void verifyInkOnGlass (const juce::Image& with, const juce::Image& without)
{
    const auto w = static_cast<float> (with.getWidth()), h = static_cast<float> (with.getHeight());
    const juce::Image::BitmapData filled (with, juce::Image::BitmapData::readOnly), empty (without, juce::Image::BitmapData::readOnly);
    int painted = 0;
    for (int y = static_cast<int> (h * 0.45f); y < static_cast<int> (h * 0.72f); ++y)
        for (int x = 0; x < with.getWidth(); ++x)
        {
            if (filled.getPixelColour (x, y) == empty.getPixelColour (x, y)) continue;
            ++painted;
            const auto row = static_cast<float> (y) * 1024.0f / h, column = static_cast<float> (x) * 1536.0f / w;
            check (row <= 687.0f, "no word paints on the gold rim");
            check (column >= inset (row) - 4.0f && column <= 1540.0f - inset (row), "no word paints on the curved rim");
        }
    check (painted > 0, "positive control: the zero and the song really were drawn");
}

inline void verifyRimPixels (observatory::Role role, Face face, int width, float scale,
                             const std::shared_ptr<const metric_face::MetricChassisPair>& bank)
{
    observatory::View view (role, bank);
    prepare (view, role, width, face);
    context = juce::String ("rim pixels ") + metric_needle::label (face) + " " + juce::String (width) + " x" + juce::String (scale, 2);
    if (face == Face::lu)
    {
        auto facts = reference();
        facts.song = juce::String::fromUTF8 (u8"\u2588\u2588\u2588 gjpqy long long long long name");
        view.lu().setFacts (facts);
        const auto filled = renderAt (view, scale);
        facts.song = ""; view.lu().setFacts (facts);
        const auto without = renderAt (view, scale);
        verifyInkOnGlass (filled, without);
        facts.zero = lu_needle::Zero::noSong; view.lu().setFacts (facts);
        verifyInkOnGlass (renderAt (view, scale), without);
        return;
    }
    const auto scene = [&] (const char* song) {
        Scene value { role, face, Status::ready, "rim", juce::String::fromUTF8 (song) };
        return frameFor (value);
    };
    view.setMetricFrame (scene (u8"\u2588\u2588\u2588 gjpqy"));
    const auto filled = renderAt (view, scale);
    auto other = scene ("");
    other.median += 0.01; other.own += 0.01; other.pre += 0.01; // the same needles, another zero
    view.setMetricFrame (other);
    verifyInkOnGlass (filled, renderAt (view, scale));
}

// The header's source name: all of it while it fits, and an ellipsis (never a silent cut) when it does not.
inline void verifyHeaderName (observatory::Role role, Face face, int width, bool recording, const juce::String& name,
                              const std::shared_ptr<const metric_face::MetricChassisPair>& bank)
{
    observatory::View view (role, bank);
    prepare (view, role, width, face);
    view.setConnection (name, COL_LED_BLUE, role == observatory::Role::pre ? observatory::ConnectionState::source
                                                                          : observatory::ConnectionState::paired);
    view.setHostRecording (recording);
    context = "header " + name + " " + metric_needle::label (face) + " " + juce::String (width) + (recording ? " REC" : "");
    text_style::ShownTextLog log;
    const auto image = renderAt (view, 1.0f);
    if (face == Face::vu || face == Face::psr)
        preview (image, "header-" + juce::String (metric_needle::label (face)) + "-" + juce::String (width) + (recording ? "-rec-" : "-") + name.substring (0, 12).replaceCharacter (' ', '_'));
    const auto font = labelFont (presentation::forEditor (width, width * 2 / 3), typography::TextRole::status);
    bool found = false;
    for (int i = 0; i < log.texts().size(); ++i)
    {
        const auto text = log.texts()[i];
        const auto cut = text.endsWithChar (0x2026) && name.startsWith (text.dropLastCharacters (1).trimEnd()) && text.length() > 3;
        if (text != name && ! cut) continue;
        found = true;
        const auto area = log.areas()[static_cast<std::size_t> (i)];
        check (font.getStringWidthFloat (text) <= area.getWidth() + 0.5f, "the drawn name fits its place");
        check (view.getLocalBounds().toFloat().contains (area), "the name stays in the window");
        check (cut || text == name, "a name is cut only with an ellipsis");
        check (! cut || name.length() > 24 || recording || width < 450, "an ordinary name is shown whole");
    }
    check (found, "the source name is drawn");
}

inline void run (const std::shared_ptr<const metric_face::MetricChassisPair>& bank)
{
    using observatory::Role;
    const auto longSong = juce::String::fromUTF8 (u8"とても長い曲の名前がここに入りますので最後までは入りきりません A very long title that cannot fit");
    const bool previews = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {}).isNotEmpty();
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const juce::String lang = language == i18n::Language::english ? "en" : "ja";
        for (const auto role : { Role::pre, Role::post })
            for (const auto face : { Face::psr, Face::crest, Face::sharp })
            {
                const auto name = [&] (const char* state) {
                    return juce::String (role == Role::pre ? "pre-" : "post-") + metric_needle::label (face) + "-" + state + "-" + lang;
                };
                std::vector<Scene> scenes {
                    { role, face, Status::ready, name ("ready"), "Synthetic Cue" },
                    { role, face, Status::ready, name ("long-song"), longSong },
                    { role, face, Status::ready, name ("no-song"), "" },
                    { role, face, Status::measuring, name ("preparing"), "Synthetic Cue" },
                    { role, face, Status::measuring, name ("preparing-long"), longSong },
                    { role, face, Status::unavailable, name ("unavailable"), "Synthetic Cue", "read_failed" } };
                // The same places hold every reason: one face covers the remaining ones.
                if (face == Face::psr) for (const auto* code : { "select_reference_post", "unsupported_format", "source_changed", "source_unavailable", "private_copy_required",
                                          "capacity_exceeded", "audio_too_short", "cue_too_short", "no_valid_value", "analysis_failed" })
                    if (role == Role::pre || juce::String (code) != "select_reference_post") // only a PRE is told where to choose
                        scenes.push_back ({ role, face, Status::unavailable, name (code), "Synthetic Cue", code });
                for (const int width : { 300, 450, 600, 900 })
                    for (const auto& scene : scenes)
                    {
                        verifyScene (scene, width, bank);
                        if (! previews || (face == Face::sharp && role == Role::pre)) continue;
                        observatory::View view (role, bank);
                        prepare (view, role, width, face);
                        view.setMetricFrame (frameFor (scene));
                        if (scene.name.contains ("-ready") || scene.name.contains ("-long-song") || scene.name.contains ("-preparing-") || scene.name.contains ("-unavailable"))
                            preview (renderAt (view, 1.0f), "embedded-" + scene.name + "-" + juce::String (width));
                    }
            }
    }
    for (const auto role : { Role::pre, Role::post })
        for (const auto face : { Face::psr, Face::lu })
            for (const int width : { 300, 450, 600, 900 })
                for (const auto scale : { 1.0f, 1.25f, 1.5f, 2.0f, 3.0f })
                    if (! (width == 900 && scale > 1.5f) && ! (width == 600 && scale > 2.0f))
                        verifyRimPixels (role, face, width, scale, bank);
    for (const auto face : { Face::vu, Face::lu, Face::psr, Face::crest })
        for (const int width : { 300, 450, 600, 900 })
            for (const bool recording : { false, true })
                for (const auto* name : { "SOURCE MIXABCQA", "SOURCE MWMWMWMQA", "PAIR DRUM BUS 1", "SOURCE A VERY LONG TRACK NAME THAT CANNOT FIT" })
                    verifyHeaderName (Role::pre, face, width, recording, name, bank);
}
}
