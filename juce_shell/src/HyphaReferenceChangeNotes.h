#pragma once
#include "reference_audition/ReferenceChangeNotesModel.h"
#include "HyphaTextButton.h"
#include "HyphaReferenceChangeNoteRows.h"
#include "HyphaLanguage.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace hypha::reference_ui
{
// Message-thread, local presentation only. No transport, gain, role or file callback.
class ChangeNotesView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(bool)> onExpansionChanged; // Layout only; never audio control.
    bool isExpanded() const noexcept { return opened; }
    int minimumHeight() const noexcept { return opened ? 90 : 44; }
    ChangeNotesView()
    {
        setOpaque(true);
        setComponentID("reference-change-notes");
        list.setComponentID("reference-change-notes-list");
        list.onSelect=[this](const juce::String& noteId) {
            if(!data) return;
            const auto found=std::find_if(data->notes.begin(),data->notes.end(),[&noteId](const auto& note){ return note.noteId==noteId; });
            if(found!=data->notes.end()) selectNote(static_cast<size_t>(found-data->notes.begin()));
        };
        addChildComponent(list);
        body.setComponentID("reference-change-notes-body");
        body.setMultiLine(true, true); body.setReadOnly(true); body.setScrollbarsShown(true);
        body.setColour(juce::TextEditor::backgroundColourId,BG);
        body.setColour(juce::TextEditor::textColourId,COL_NORMAL);
        addChildComponent(body);
        toggle.setComponentID("reference-change-notes-toggle");
        toggle.onClick=[this] {
            if (!data) return;
            const bool wasOpen=opened;
            if (selected>=0) { selected=-1; body.clear(); body.setTitle({}); body.setDescription({}); }
            else opened=!opened;
            refresh();
            if (wasOpen!=opened && onExpansionChanged) onExpansionChanged(opened);
        };
        addChildComponent(toggle);
    }
    void setNotes(std::shared_ptr<const reference_audition::ChangeNotes> next, bool hidden,
                  presentation::Context nextContext, const juce::String& selectionKey = {})
    {
        context=nextContext; context.referenceCompact=true;
        if (hidden || !next || next->notes.empty() || next->notes.size()>1000 || next->openCount<=0)
        { clear(); return; }
        const bool changed=data!=next || key!=selectionKey;
        if (changed) {
            clear(); data=std::move(next); key=selectionKey;
            // Open notes first in source order, then the quieter completed list.
            for (bool done : {false,true})
                for (size_t i=0;i<data->notes.size();++i)
                    if (data->notes[i].done==done) order.push_back(i);
        }
        refresh();
    }
    void clear()
    {
        // External state replacement is already inside the parent layout update.
        // Only explicit local expand/back actions may notify the parent.
        data.reset(); key.clear(); order.clear(); selected=-1; opened=false; rowWidth=0;
        list.setItems({},0); list.setViewPosition(0,0); list.setTitle({}); list.setDescription({});
        body.clear(); body.setTitle({}); body.setDescription({}); body.setTooltip({});
        toggle.setTitle({}); toggle.setDescription({}); toggle.setTooltip({}); toggle.setButtonText({});
        setTitle({}); setDescription({}); setTooltip({});
        for (auto* child:getChildren()) child->setVisible(false);
        setVisible(false); repaint();
    }
    bool selectNote(size_t index)
    {
        if (!data || index>=data->notes.size()) return false;
        const bool wasOpen=opened;
        selected=static_cast<int>(index); opened=true;
        const auto& note=data->notes[index];
        // TextEditor is plain text: markup and links are not interpreted.
        body.setText(note.text,false);
        body.setTitle(i18n::tr("Change notes")+" "+juce::String(note.number));
        body.setDescription(rowText(index));
        refresh(); if (!wasOpen && onExpansionChanged) onExpansionChanged(true); return true;
    }
    int selectedNote() const noexcept { return selected; }
    juce::String displayedBody() const { return body.getText(); }
    size_t noteCount() const noexcept { return data ? data->notes.size() : 0; }
    juce::Font toggleFont() const
    { return monoFont(context,typography::TextRole::action,typography::Composition::shell); }
    int toggleWidth() const
    {
        // Match HyphaTextButton's actual localized/native action font and 6px insets.
        return juce::jmax(72,static_cast<int>(std::ceil(text_style::shownWidth(toggleFont(),
            toggle.displayedText(),text_style::LabelPolicy::fixed)))+14);
    }
    juce::Rectangle<int> headingBounds() const
    { return getLocalBounds().removeFromTop(26).reduced(6,0).withTrimmedRight(toggleWidth()+6); }
    juce::Rectangle<int> sourceLabelBounds() const
    {
        const auto text=i18n::tr("Source time");
        return {6,22,juce::roundToInt(std::ceil(fontFor(text).getStringWidthFloat(text)))+2,20};
    }
    juce::Rectangle<int> timelineBounds() const
    {
        const int left=sourceLabelBounds().getRight()+6;
        return {left,22,juce::jmax(0,getWidth()-left-6),20};
    }
    static juce::String sourceTime(double seconds)
    {
        const auto whole=static_cast<juce::int64>(std::floor(juce::jmax(0.0,seconds)));
        return juce::String(whole/60)+":"+juce::String(whole%60).paddedLeft('0',2);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto axis=timelineBounds();
        if (!data || opened || !axis.contains(event.getPosition())) return;
        const auto at=(event.position.x-axis.getX())/juce::jmax(1.0f,static_cast<float>(axis.getWidth()));
        double distance=2; size_t nearest=data->notes.size();
        for (size_t i=0;i<data->notes.size();++i) if (!data->notes[i].done) {
            const auto d=std::abs(position(data->notes[i])-at);
            if (d<distance) { distance=d; nearest=i; }
        }
        if (nearest<data->notes.size()) selectNote(nearest);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(BG);
        if (!data) return;
        const auto header=headingBounds();
        const auto heading=selected>=0 ? i18n::tr("Source time")+" "+sourceTime(data->notes[static_cast<size_t>(selected)].atSeconds)
            +"  #"+juce::String(data->notes[static_cast<size_t>(selected)].number)
            : i18n::tr("Change notes")+" "+juce::String(data->openCount);
        draw(g,heading,header,COL_NORMAL);
        if (opened) return;
        draw(g,i18n::tr("Source time"),sourceLabelBounds(),COL_TEXT_SECONDARY);
        const auto axis=timelineBounds();
        g.setColour(COL_MUTED); g.drawHorizontalLine(axis.getBottom()-2,axis.getX(),axis.getRight());
        int right=axis.getX()-1;
        for (const auto& note:data->notes) if (!note.done) {
            const int x=axis.getX()+juce::roundToInt(position(note)*(axis.getWidth()-1));
            g.setColour(COL_NORMAL); g.drawVerticalLine(x,axis.getBottom()-5,axis.getBottom());
            const auto number=juce::String(note.number);
            const int width=juce::roundToInt(fontFor(number).getStringWidthFloat(number))+6;
            const int left=juce::jlimit(axis.getX(),juce::jmax(axis.getX(),axis.getRight()-width),x-width/2);
            // Crowded marks retain their exact positions; all numbers remain in the virtual list.
            if (left>=right+3) { draw(g,number,{left,axis.getY(),width,18},COL_NORMAL); right=left+width; }
        }

    }
    void resized() override
    {
        list.setVisible(data && opened && selected<0);
        body.setVisible(data && selected>=0);
        const int actionWidth=toggleWidth();
        toggle.setBounds(juce::jmax(0,getWidth()-actionWidth-6),2,actionWidth,24);
        auto area=getLocalBounds().withTrimmedTop(28).reduced(6,0);
        list.setBounds(area); body.setBounds(area);
        if (opened && selected<0) updateWrappedRowHeight();
    }
private:
    double duration() const { return data && data->sampleRateHz>0 ? static_cast<double>(data->totalSampleFrames)/data->sampleRateHz : 0; }
    double position(const reference_audition::ChangeNote& note) const
    { return data && data->totalSampleFrames>1 ? juce::jlimit(0.0,1.0,static_cast<double>(note.atSample)/(data->totalSampleFrames-1)) : 0; }
    juce::Font fontFor(const juce::String& text) const
    { return displayTextFont(text,context,typography::TextRole::listenChoice,typography::Composition::information); }
    void draw(juce::Graphics& g,const juce::String& text,juce::Rectangle<int> bounds,juce::Colour colour) const
    {
        g.setColour(colour);
        g.setFont(displayTextFont(text,context,typography::TextRole::listenChoice,typography::Composition::information));
        text_style::drawText(g,text,bounds.toFloat(),juce::Justification::centredLeft,true,text_style::LabelPolicy::fixed);
    }
    juce::String rowText(size_t index) const
    {
        const auto& note=data->notes[index];
        return (note.done ? i18n::tr("Done")+" " : juce::String{})+juce::String(note.number)+"  "
            +sourceTime(note.atSeconds)+"  "+note.text;
    }
    void updateWrappedRowHeight()
    {
        const int width=juce::jmax(1,list.getWidth()-list.getScrollBarThickness());
        const auto revision=i18n::revision();
        if (width==rowWidth && revision==rowLanguage && context==rowContext) return;
        rowWidth=width; rowLanguage=revision; rowContext=context;
        std::vector<ChangeNoteRows::Item> rows;
        rows.reserve(order.size());
        for(const auto index:order)
        {
            auto text=rowText(index);
            const auto font=fontFor(text);
            const auto colour=data->notes[index].done ? COL_TEXT_SECONDARY : COL_NORMAL;
            const auto measure=[&](const juce::String& candidate) {
                return ChangeNoteRows::layout(candidate,font,colour,width-8);
            };
            auto wrapped=measure(text);
            if(wrapped.getNumLines()>3)
            {
                const auto ellipsis=juce::String::fromUTF8(u8"…");
                int low=0, high=text.length();
                while(low<high)
                {
                    const int mid=low+(high-low+1)/2;
                    if(measure(text.substring(0,mid).trimEnd()+ellipsis).getNumLines()<=3) low=mid;
                    else high=mid-1;
                }
                text=text.substring(0,low).trimEnd()+ellipsis;
                wrapped=measure(text);
            }
            rows.push_back({data->notes[index].noteId,std::move(text),font,colour,juce::jmax(24,
                static_cast<int>(std::ceil(wrapped.getHeight()))+6)});
        }
        list.setItems(std::move(rows),width);
    }
    void refresh()
    {
        if (!data) return;
        setVisible(true); setTitle(i18n::tr("Change notes"));
        setDescription(i18n::tr("Source time")+"; "+juce::String(data->openCount));
        toggle.setPresentationContext(context);
        toggle.setButtonText(opened ? "Back" : "Details");
        toggle.setTitle(i18n::tr("Change notes")); toggle.setVisible(true);
        list.setTitle(i18n::tr("Change notes"));
        list.setVisible(opened && selected<0);
        body.setFont(displayTextFont(body.getText(),context,typography::TextRole::listenChoice,typography::Composition::information));
        body.applyFontToAllText(body.getFont());
        body.setVisible(selected>=0); resized(); repaint();
    }
    std::shared_ptr<const reference_audition::ChangeNotes> data;
    juce::String key;
    std::vector<size_t> order;
    int selected=-1; bool opened=false;
    presentation::Context context=presentation::defaultContext();
    ChangeNoteRows list;
    int rowWidth=0;
    unsigned rowLanguage=0;
    presentation::Context rowContext=presentation::defaultContext();
    juce::TextEditor body;
    HyphaTextButton toggle {"Change notes"};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChangeNotesView)
};
}
