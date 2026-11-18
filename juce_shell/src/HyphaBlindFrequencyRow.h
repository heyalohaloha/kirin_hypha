#pragma once
#include "HyphaBlindDetailsCollector.h"
#include "HyphaSpectrumGeometry.h"
#include "HyphaLanguage.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include <memory>

namespace hypha::blind_ui
{
// Frozen message-thread observations. No smoothing, re-binning or audio changes.
class FrequencyRow : public juce::Component
{
public:
    void setResult (std::shared_ptr<const blind_details::Result> next)
    { if (data != next) { data = std::move(next); pointer = -1; repaint(); } }
    bool valid (size_t bin) const noexcept
    { return data && bin < data->frequency.size() && data->frequency[bin].sufficient
        && std::isfinite(data->frequency[bin].median); }
    bool available() const noexcept
    {
        if (!data || !std::isfinite(data->minHz) || !std::isfinite(data->maxHz)
            || data->minHz <= 0 || data->maxHz <= data->minHz) return false;
        for (size_t i=0;i<data->frequency.size();++i) if (valid(i)) return true;
        return false;
    }
    presentation::Context textContext() const { return presentation::forEditor(getWidth(),getHeight()); }
    float textHeight() const
    { return displayTextFont(i18n::tr("Frequency difference (POST - PRE)"),textContext(),typography::TextRole::listenChoice,typography::Composition::information).getHeight(); }
    int rowHeight() const { return juce::roundToInt(std::ceil(textHeight()))+4; }
    juce::Rectangle<int> headingBounds() const { return getLocalBounds().removeFromTop(rowHeight()); }
    juce::Rectangle<int> readoutBounds() const { return getLocalBounds().withTrimmedTop(rowHeight()+2).removeFromTop(rowHeight()); }
    juce::Rectangle<float> plotBounds() const
    {
        const auto axisFont=displayTextFont("+24",textContext(),typography::TextRole::listenChoice,typography::Composition::information);
        const auto margin=std::ceil(std::max(axisFont.getStringWidthFloat("+24"),axisFont.getStringWidthFloat("-24")))+8.0f;
        return getLocalBounds().toFloat().withTrimmedLeft(margin).withTrimmedRight(22)
            .withTrimmedTop(2.5f*rowHeight()+2.0f).withTrimmedBottom(static_cast<float>(rowHeight()+4));
    }
    juce::Rectangle<float> frequencyLabelBounds(float hz) const
    {
        const auto plot=plotBounds();
        const auto label=hz<1000?juce::String(hz,0):juce::String(hz/1000,0)+"k";
        const auto width=std::ceil(displayTextFont(label,textContext(),typography::TextRole::listenChoice,typography::Composition::information).getStringWidthFloat(label))+4;
        const auto x=spectrum_geometry::xForFrequency(hz,data->minHz,data->maxHz,plot);
        return {x-width*.5f,plot.getBottom()+4,width,static_cast<float>(rowHeight())};
    }
    // Same centre mapping, adjacent-bin interpolation and missing-neighbour rule as FREQ's pointer.
    juce::String pointerText (float normalised) const
    {
        if (!available()) return i18n::tr("Not shown because the measured time is too short");
        const auto x=spectrum_geometry::clampToBandCentreRange(normalised);
        const auto position=spectrum_geometry::bandPositionForNormalisedX(x);
        const auto lower=static_cast<size_t>(std::floor(position));
        const auto upper=std::min(lower+1,data->frequency.size()-1);
        const auto blend=position-static_cast<float>(lower);
        const auto hz=spectrum_geometry::frequencyForNormalisedX(x,data->minHz,data->maxHz);
        const auto prefix=juce::String(hz,1)+" Hz: ";
        if (!valid(lower) || (blend>=0.0001f && upper!=lower && !valid(upper)))
            return prefix+i18n::tr("Not shown because the measured time is too short");
        const auto value=blend<0.0001f ? data->frequency[lower].median
            : juce::jmap(static_cast<double>(blend),data->frequency[lower].median,data->frequency[upper].median);
        return prefix+(value>=0 ? "+" : "")+juce::String(value,2)+" dB";
    }
    void mouseMove (const juce::MouseEvent& e) override
    { const auto plot=plotBounds(); pointer=plot.contains(e.position) ? (e.position.x-plot.getX())/plot.getWidth() : -1; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { pointer=-1; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto context=textContext();
        g.setFont(displayTextFont(i18n::tr("Frequency difference (POST - PRE)"),context,typography::TextRole::listenChoice,typography::Composition::information));
        g.setColour(COL_NORMAL);
        text_style::drawText(g,i18n::tr("Frequency difference (POST - PRE)"),headingBounds(),juce::Justification::centredLeft);
        if (!available())
        { text_style::drawLines(g,i18n::tr("Not shown because the measured time is too short"),getLocalBounds().withTrimmedTop(rowHeight()+4),juce::Justification::topLeft,4); return; }
        const auto plot=plotBounds();
        // Numeric Hz/dB values are language-neutral; insufficient-data text is already localized.
        if (pointer>=0) text_style::drawText(g,pointerText(pointer),readoutBounds().toFloat(),juce::Justification::centredLeft,true,text_style::LabelPolicy::fixed);
        g.setFont(displayTextFont("+24",context,typography::TextRole::listenChoice,typography::Composition::information));
        g.setColour(COL_MUTED);
        g.drawHorizontalLine(juce::roundToInt(spectrum_geometry::yForDeltaDb(0,plot)),plot.getX(),plot.getRight());
        for (const float db : {-KIRIN_SPECTRUM_DISPLAY_RANGE_DB,0.0f,KIRIN_SPECTRUM_DISPLAY_RANGE_DB})
        {
            const auto y=spectrum_geometry::yForDeltaDb(db,plot);
            text_style::drawText(g,(db>0?"+":"")+juce::String(db,0),juce::Rectangle<float>(0,y-rowHeight()*.5f,plot.getX()-6,static_cast<float>(rowHeight())),juce::Justification::centredRight);
        }
        for (const float hz : {100.0f,1000.0f,10000.0f})
        {
            if (hz<=data->minHz || hz>=data->maxHz) continue;
            text_style::drawText(g,hz<1000?juce::String(hz,0):juce::String(hz/1000,0)+"k",
                frequencyLabelBounds(hz),juce::Justification::centred,true,text_style::LabelPolicy::fixed);
        }
        juce::Path line; bool continuing=false;
        for(size_t i=0;i<data->frequency.size();++i)
        {
            if(!valid(i)) { continuing=false; continue; }
            const auto x=juce::jmap(spectrum_geometry::bandCentreNormalisedX(i),plot.getX(),plot.getRight());
            const auto y=spectrum_geometry::yForDeltaDb(static_cast<float>(data->frequency[i].median),plot);
            if(continuing) line.lineTo(x,y); else line.startNewSubPath(x,y);
            continuing=true;
        }
        g.setColour(COL_NORMAL); g.strokePath(line,juce::PathStrokeType(1.5f));
    }
private:
    std::shared_ptr<const blind_details::Result> data;
    float pointer=-1;
};
}
