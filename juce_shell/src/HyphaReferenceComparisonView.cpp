#include "HyphaReferenceComparisonView.h"
#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include <algorithm>
#include <cmath>
namespace hypha::reference_ui
{
namespace
{
double linear (std::int64_t milli) { return std::pow (10.0, double (milli) / 20000.0); }
double peak (const KirinReferenceVisualBin& bin) { return std::max (bin.peak[0], bin.peak[1]); }
double rms (const KirinReferenceVisualBin& bin, int channels)
{ return std::sqrt ((bin.rms[0]*bin.rms[0] + bin.rms[1]*bin.rms[1]) / channels); }
juce::String timeText (double seconds)
{ return juce::String (int (seconds) / 60) + ":" + juce::String (int (seconds) % 60).paddedLeft ('0', 2); }
}
void ComparisonView::ViewButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1);
    g.setColour (COL_MUTED.withAlpha (down ? 0.3f : highlighted ? 0.16f : 0.07f));
    g.fillRoundedRectangle (bounds, 3);
    g.setColour (getToggleState() ? COL_FLORA : COL_TEXT_SECONDARY);
    g.setFont (labelFont (presentation::forEditor (300, 200), typography::TextRole::legend,
        typography::Composition::visualization).withHeight (10.5f));
    g.drawText (getButtonText(), bounds, juce::Justification::centred);
}
ComparisonView::ComparisonView()
{
    setComponentID ("reference-comparison-view"); setWantsKeyboardFocus (true);
    setTitle ("A and B comparison. Arrows move the view; Shift and arrows resize it; Home follows playback.");
    for (auto* button : { &follow, &loudness, &crest }) addAndMakeVisible (*button);
    follow.setComponentID ("reference-follow"); loudness.setComponentID ("reference-loudness"); crest.setComponentID ("reference-crest");
    follow.onClick = [this] { following = true; saveView(); repaint(); };
    loudness.onClick = [this] { showingCrest = false; saveView(); repaint(); };
    crest.onClick = [this] { showingCrest = true; saveView(); repaint(); };
}
void ComparisonView::update (std::shared_ptr<const reference_audition::VisualTimeline> next,
    double currentPosition, presentation::Context presentation, bool concealed, std::shared_ptr<reference_audition::VisualPreferences> saved)
{
    context = presentation; hidden = concealed; preferences = std::move (saved);
    if (concealed) { data.reset(); waveformCache = {}; setTitle ({}); return; }
    data = std::move (next); position = currentPosition;
    if (data && data->binding.key != key)
    {
        key = data->binding.key; following = true; start = 0; end = std::min (12.0, data->duration()); cacheRevision = 0;
        const auto choice = preferences ? preferences->get() : reference_audition::VisualViewChoice {};
        if (choice.valid() && data->binding.source && data->binding.aligned
            && choice.sourceHash == data->binding.source->sourceFileSha256 && choice.end <= data->duration())
        { start = choice.start; end = choice.end; following = choice.follow; showingCrest = choice.crest; }
        else if (choice.valid() && data->binding.aligned && data->binding.source && lastVerifiedView.source
            && lastVerifiedView.hostRate > 0 && data->binding.hostRate > 0
            && choice.sourceHash == lastVerifiedView.source->sourceFileSha256
            && data->binding.source->sourceKind == "work_version" && lastVerifiedView.source->sourceKind == "work_version")
        {
            const auto oldWork = lastVerifiedView.source->sourceIdentityKey.upToFirstOccurrenceOf (":", false, false);
            const auto newWork = data->binding.source->sourceIdentityKey.upToFirstOccurrenceOf (":", false, false);
            if (oldWork.isNotEmpty() && oldWork == newWork)
            {
                // Carry the selected DAW interval through two independently verified maps.
                const double shift = (double(lastVerifiedView.hostAnchor)-double(lastVerifiedView.sourceAnchor))/lastVerifiedView.hostRate
                    + (double(data->binding.sourceAnchor)-double(data->binding.hostAnchor))/data->binding.hostRate;
                setRange (choice.start+shift, choice.end+shift);
                following = choice.follow; showingCrest = choice.crest; saveView();
            }
        }
    }
    if (data && following && position >= 0 && position <= data->duration())
        setRange (position - 6.0, position + 6.0);
    if (data && data->binding.aligned) lastVerifiedView = data->binding;
    setTitle ("A and B comparison. Arrows move; Shift and arrows resize; Home follows playback.");
    resized(); repaint();
}
void ComparisonView::saveView()
{
    if (preferences && data && data->binding.source && !hidden)
        preferences->set ({ data->binding.source->sourceFileSha256, start, end, following, showingCrest });
}
void ComparisonView::setRange (double first, double last)
{
    if (!data || data->duration() <= 0) return;
    const double duration = data->duration(), width = juce::jlimit (std::min (1.0, duration), duration, last - first);
    start = juce::jlimit (0.0, duration - width, first); end = start + width;
}
void ComparisonView::resized()
{
    const auto previous = waveform;
    auto area = getLocalBounds().toFloat().reduced (5);
    const bool detail = getHeight() >= 140 && getWidth() >= 380;
    auto toolbar = area.removeFromTop (getHeight() >= 65 ? 18.0f : 0.0f);
    follow.setBounds (toolbar.removeFromRight (60).toNearestInt());
    follow.setVisible (getHeight() >= 65 && !hidden);
    waveform = area.removeFromTop (detail ? area.getHeight() * 0.46f : juce::jmax (8.0f, area.getHeight() - 17));
    waveform.removeFromLeft (15);
    auto tabs = area.removeFromTop (20);
    loudness.setBounds (tabs.removeFromLeft (84).toNearestInt());
    crest.setBounds (tabs.removeFromLeft (58).toNearestInt());
    loudness.setVisible (detail && !hidden); crest.setVisible (detail && !hidden);
    graph = detail ? area.reduced (15, 3) : juce::Rectangle<float> {};
    if (previous != waveform) cacheRevision = 0;
}
void ComparisonView::rebuild()
{
    waveformCache = {};
    if (!data || !data->binding.source || !data->binding.overview || !data->binding.overview->waveform) return;
    const auto& source = *data->binding.source;
    const auto& overview = *data->binding.overview->waveform;
    if (overview.samplePeakMillidbfs.empty() || overview.framesPerBin < 1 || source.audio.totalSampleFrames < 1) return;
    const auto count = overview.samplePeakMillidbfs.front().size();
    const int columns = juce::jlimit (1, 900, int (std::ceil (waveform.getWidth())));
    const int height = juce::jmax (1, int (std::ceil (waveform.getHeight())));
    const double gain = std::pow (10.0, data->binding.gainDb / 20.0);
    std::array<std::vector<double>,6> radii;
    for (auto& values : radii) values.assign (size_t (columns), std::numeric_limits<double>::quiet_NaN());
    double scale = 1.0;
    for (int x=0; x<columns; ++x)
    {
        const double first = double(x) * double(source.audio.totalSampleFrames) / columns;
        const double last = double(x+1) * double(source.audio.totalSampleFrames) / columns;
        double ap=0, ae=0, bp=0, be=0, covered=0;
        bool historical = false;
        for (size_t i=size_t(first / overview.framesPerBin); i<count && double(i)*overview.framesPerBin<last; ++i)
        {
            const double frames = std::max (0.0, std::min (last,double(i+1)*overview.framesPerBin) - std::max (first,double(i)*overview.framesPerBin));
            double p=0, energy=0;
            for (size_t c=0;c<overview.samplePeakMillidbfs.size();++c)
            {
                if (i<overview.samplePeakMillidbfs[c].size()) p=std::max(p,linear(overview.samplePeakMillidbfs[c][i]));
                if (c<overview.rmsMillidbfs.size() && i<overview.rmsMillidbfs[c].size())
                { const double v=linear(overview.rmsMillidbfs[c][i]); energy+=v*v; }
            }
            energy /= double(source.audio.channels);
            if (i<data->bins.size() && data->bins[i].pass)
            {
                const auto& bin=data->bins[i]; const auto ar=rms(bin.a,data->binding.channels);
                ap=std::max(ap,peak(bin.a)); ae+=ar*ar*frames; covered+=frames;
                historical = historical || bin.pass != data->pass;
                p=peak(bin.b); const auto br=rms(bin.b,data->binding.channels); energy=br*br;
            }
            bp=std::max(bp,p); be+=energy*frames;
        }
        const auto column=size_t(x);
        radii[2][column]=bp*gain; radii[3][column]=std::sqrt(be/(last-first))*gain;
        if (covered>=last-first-1e-6)
        { radii[historical ? 4 : 0][column]=ap; radii[historical ? 5 : 1][column]=std::sqrt(ae/covered); }
        scale=std::max({scale,ap,bp*gain});
    }
    // A single shared amplitude scale; peak is max and RMS is frame-weighted energy.
    // Pixel reduction affects the drawing only. Readouts always use actual bins.
    scale=std::pow(2.0,std::ceil(std::log2(scale)));
    waveformCache=juce::Image(juce::Image::ARGB,columns,height,true);
    juce::Graphics drawing(waveformCache);
    const std::array<juce::Colour,6> colours { COL_SPECTRUM_DELTA_BR.withAlpha(0.45f),COL_SPECTRUM_DELTA_BR.withAlpha(0.90f),
        COL_FLORA.withAlpha(0.45f),COL_FLORA.withAlpha(0.90f),COL_SPECTRUM_DELTA_BR.withAlpha(0.14f),COL_SPECTRUM_DELTA_BR.withAlpha(0.28f) };
    for (size_t layer=0;layer<radii.size();++layer)
    {
        drawing.setColour(colours[layer]); const double center=height*((layer==2 || layer==3) ? 0.75 : 0.25);
        for (int x=0;x<columns;++x) if (std::isfinite(radii[layer][size_t(x)]))
        {
            const auto radius=std::max(0.5,radii[layer][size_t(x)]/scale*height*0.2);
            drawing.fillRect(x,int(std::floor(center-radius)),1,juce::jmax(1,int(std::ceil(radius*2))));
        }
    }
    cacheRevision=data->revision;
}
juce::String ComparisonView::valuesAt (double seconds, bool compact) const
{
    if (!data || !data->binding.aligned || data->hop <= 0 || seconds < 0 || seconds > data->duration()) return "A  --    B  --";
    const auto& source = *data->binding.source;
    // Values belong to completed bin endpoints; no interpolation or mean LUFS.
    const auto raw = seconds >= data->duration() ? double (data->bins.size()) - 1.0
        : std::floor (seconds * source.audio.sampleRateHz / data->hop) - 1.0;
    if (raw < 0 || raw >= double (data->bins.size())) return "A  --    B  --";
    const auto& bin = data->bins[size_t (raw)];
    if (!bin.pass || bin.pass != data->pass) return "A  --    B  --";
    const double a = showingCrest ? bin.a.crest_db : bin.a.short_lufs;
    const double b = showingCrest ? bin.b.crest_db : bin.b.short_lufs + data->binding.gainDb;
    if (!std::isfinite (a) || !std::isfinite (b)) return "A  --    B  --";
    if (compact) return "B-A " + juce::String (b >= a ? "+" : "") + juce::String (b-a, 1)
        + (showingCrest ? " dB / CREST" : " LU / 3s");
    const auto endpoint = double (std::min (source.audio.totalSampleFrames, (std::int64_t (raw)+1)*data->hop)) / source.audio.sampleRateHz;
    return juce::String (endpoint, 1) + "s  A " + juce::String (a, 1) + "   B " + juce::String (b, 1)
        + "   B-A " + (b >= a ? "+" : "") + juce::String (b-a, 1) + (showingCrest ? " dB" : " LU");
}
void ComparisonView::paint (juce::Graphics& g)
{
    if (hidden) return;
    surface_material::paintObservationWell (g, getLocalBounds().toFloat());
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization).withHeight (getWidth() >= 600 ? 12.0f : 10.5f));
    g.setColour (COL_TEXT_SECONDARY);
    const bool detail = !graph.isEmpty();
    const auto heading = data && data->binding.aligned
        ? (data->binding.matched ? "MATCHED" : "ORIGINAL") : "B OVERVIEW";
    if (getHeight() >= 65) text_style::drawEllipsized (g, heading, juce::Rectangle<int> (7, 3, juce::jmax (0, getWidth() - 76), 18), juce::Justification::centredLeft);
    if (!data || !data->binding.overview || !data->binding.overview->waveform)
    { text_style::drawEllipsized (g, "Choose Version", getLocalBounds().reduced (20), juce::Justification::centred); return; }
    if (cacheRevision != data->revision) rebuild();
    if (waveformCache.isValid()) g.drawImageAt (waveformCache, int(waveform.getX()), int(waveform.getY()));
    g.setColour (COL_TEXT_SECONDARY);
    if (waveform.getHeight() >= 24) g.drawText ("A", juce::Rectangle<float> (4, waveform.getY(), 12, waveform.getHeight()*0.5f), juce::Justification::centred);
    if (waveform.getHeight() >= 24) g.drawText ("B", juce::Rectangle<float> (4, waveform.getCentreY(), 12, waveform.getHeight()*0.5f), juce::Justification::centred);
    const auto duration = data->duration();
    if (duration > 0)
    {
        const float x = waveform.getX() + float (start / duration) * waveform.getWidth();
        const float width = float ((end - start) / duration) * waveform.getWidth();
        g.setColour (COL_NORMAL.withAlpha (0.08f)); g.fillRect (x, waveform.getY(), width, waveform.getHeight());
        g.setColour (COL_NORMAL.withAlpha (0.40f)); g.drawRect (juce::Rectangle<float> (x, waveform.getY(), width, waveform.getHeight()));
        if (data->binding.aligned && position >= 0 && position <= duration)
        { g.setColour (COL_NORMAL); g.drawVerticalLine (int (waveform.getX() + float (position / duration)*waveform.getWidth()), waveform.getY(), waveform.getBottom()); }
    }
    follow.setToggleState (following, juce::dontSendNotification);
    loudness.setToggleState (!showingCrest, juce::dontSendNotification);
    crest.setToggleState (showingCrest, juce::dontSendNotification);
    if (detail) paintDetails (g);
    else
    {
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, valuesAt (position, true), getLocalBounds().removeFromBottom (17).reduced (6, 0), juce::Justification::centredLeft);
    }
}
void ComparisonView::paintDetails (juce::Graphics& g)
{
    const auto time = pointedTime >= 0 ? pointedTime : position;
    g.setColour (COL_TEXT_SECONDARY);
    const auto rangeText = timeText (start) + " - " + timeText (end);
    g.drawText (rangeText, juce::Rectangle<float> (float (getWidth()-110), graph.getY()-23, 102, 18), juce::Justification::centredRight);
    auto chart = graph; chart.removeFromBottom (18);
    double minimum = showingCrest ? 0.0 : -24.0, maximum = showingCrest ? 18.0 : -6.0;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    if (data && data->binding.source) for (size_t i=0; i<data->bins.size(); ++i)
    {
        const auto& pair = data->bins[i];
        const auto seconds = double (std::min (std::int64_t (i+1)*data->hop, data->binding.source->audio.totalSampleFrames)) / data->binding.source->audio.sampleRateHz;
        if (!pair.pass || pair.pass != data->pass || seconds < start || seconds > end) continue;
        const std::array<double,2> values { showingCrest ? pair.a.crest_db : pair.a.short_lufs,
            showingCrest ? pair.b.crest_db : pair.b.short_lufs + data->binding.gainDb };
        for (auto value : values) if (std::isfinite (value)) { low = std::min (low,value); high = std::max (high,value); }
    }
    if (std::isfinite (low))
    {
        const auto center = (low+high)*0.5, span = std::max (6.0, high-low+2.0);
        minimum = std::floor (center-span*0.5); maximum = std::ceil (center+span*0.5);
    }
    for (int i = 1; i < 4; ++i)
    {
        const auto y = chart.getY()+chart.getHeight()*i/4;
        g.setColour (COL_MUTED.withAlpha (0.12f)); g.drawHorizontalLine (int (y), chart.getX(), chart.getRight());
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.65f));
        g.drawText (juce::String (maximum-(maximum-minimum)*i/4,1), juce::Rectangle<float> (chart.getX(),y-11,32,11), juce::Justification::centredLeft);
    }
    if (data && data->binding.aligned && end > start)
    {
        for (int side = 0; side < 2; ++side)
        {
            juce::Path path; bool open = false; std::uint64_t pass = 0;
            for (size_t i = 0; i < data->bins.size(); ++i)
            {
                const auto& pair = data->bins[i]; const auto& bin = side == 0 ? pair.a : pair.b;
                const double seconds = double (std::min<std::int64_t> (std::int64_t (i+1)*data->hop, data->binding.source->audio.totalSampleFrames)) / data->binding.source->audio.sampleRateHz;
                const double value = showingCrest ? bin.crest_db : bin.short_lufs + (side == 0 ? 0.0 : data->binding.gainDb);
                if (seconds < start || seconds > end || !pair.pass || pair.pass != data->pass || !std::isfinite (value)) { open = false; continue; }
                const float x = chart.getX() + float ((seconds-start)/(end-start))*chart.getWidth();
                const float y = chart.getBottom() - float (juce::jlimit (0.0, 1.0, (value-minimum)/(maximum-minimum)))*chart.getHeight();
                if (open && pass == pair.pass) path.lineTo (x,y); else path.startNewSubPath (x,y);
                open = true; pass = pair.pass;
            }
            g.setColour (side == 0 ? COL_SPECTRUM_DELTA_BR : COL_FLORA); g.strokePath (path, juce::PathStrokeType (1.2f));
        }
    }
    g.setColour (COL_TEXT_SECONDARY);
    const auto unit = showingCrest ? "TP/RMS  " : "3s  ";
    text_style::drawEllipsized (g, juce::String (unit) + valuesAt (time), graph.toNearestInt().removeFromBottom (18), juce::Justification::centredLeft);
}
double ComparisonView::timeAt (float x) const
{ return data && waveform.getWidth() > 0 ? juce::jlimit (0.0, data->duration(), double ((x-waveform.getX())/waveform.getWidth())*data->duration()) : 0; }
void ComparisonView::mouseDown (const juce::MouseEvent& event)
{
    if (!hidden && waveform.contains (event.position) && data)
    { grabKeyboardFocus(); following = false; dragAnchor = timeAt (event.position.x); setRange (dragAnchor-6, dragAnchor+6); saveView(); repaint(); }
}
void ComparisonView::mouseDrag (const juce::MouseEvent& event)
{
    if (!hidden && dragAnchor >= 0 && data && event.getDistanceFromDragStart() > 4)
    { const double t = timeAt (event.position.x); setRange (std::min (t, dragAnchor), std::max (t, dragAnchor)); saveView(); repaint(); }
}
void ComparisonView::mouseMove (const juce::MouseEvent& event)
{
    pointedTime = graph.contains (event.position) && graph.getWidth() > 0
        ? start + (end-start) * (event.position.x-graph.getX()) / graph.getWidth() : -1;
    repaint();
}
void ComparisonView::mouseExit (const juce::MouseEvent&) { pointedTime = -1; repaint(); }
bool ComparisonView::keyPressed (const juce::KeyPress& keypress)
{
    if (hidden || !data) return false;
    if (keypress.getKeyCode() == juce::KeyPress::homeKey) { following = true; saveView(); repaint(); return true; }
    const int direction = keypress.getKeyCode() == juce::KeyPress::leftKey ? -1 : keypress.getKeyCode() == juce::KeyPress::rightKey ? 1 : 0;
    if (!direction) return false;
    following = false;
    if (keypress.getModifiers().isShiftDown()) setRange (start, end + direction);
    else setRange (start + direction, end + direction);
    saveView(); repaint(); return true;
}
}
