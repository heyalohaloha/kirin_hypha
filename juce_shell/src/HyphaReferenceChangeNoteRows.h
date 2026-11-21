#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "HyphaTextStyle.h"

namespace hypha::reference_ui
{
// Variable-height, viewport-virtualized buttons. Only visible rows have actual components.
class ChangeNoteRows final : public juce::Viewport
{
public:
    struct Item { juce::String noteId, excerpt; juce::Font font; juce::Colour colour; int height = 0; };
    std::function<void(int)> onSelect;
    ChangeNoteRows() { setViewedComponent(&content,false); setScrollBarsShown(true,false); setWantsKeyboardFocus(true); }
    ~ChangeNoteRows() override { setViewedComponent(nullptr,false); }
    int rowCount() const { return static_cast<int>(items.size()); }
    int visibleButtonCount() const
    { return static_cast<int>(std::count_if(pool.begin(),pool.end(),[](const auto& row){ return row->isVisible(); })); }
    juce::Rectangle<int> rowBounds(int i) const
    { return {0,offsets[static_cast<size_t>(i)],content.getWidth(),items[static_cast<size_t>(i)].height}; }
    const juce::String& excerpt(int i) const { return items[static_cast<size_t>(i)].excerpt; }
    void setItems(std::vector<Item> next,int width)
    {
        items=std::move(next); offsets.assign(1,0);
        for(const auto& item:items) offsets.push_back(offsets.back()+item.height);
        content.setSize(width,offsets.back());
        refreshButtons();
    }
    static juce::TextLayout layout(const juce::String& text,const juce::Font& font,juce::Colour colour,int width)
    {
        juce::AttributedString attributed;
        attributed.setWordWrap(juce::AttributedString::WordWrap::byChar);
        attributed.append(text,font,colour);
        juce::TextLayout result;
        result.createLayout(attributed,static_cast<float>(juce::jmax(1,width)));
        return result;
    }
protected:
    void visibleAreaChanged(const juce::Rectangle<int>&) override { refreshButtons(); }
private:
    class Row final : public juce::Button
    {
    public:
        explicit Row(ChangeNoteRows& next):juce::Button(juce::String()),owner(next)
        {
            setWantsKeyboardFocus(true);
            onClick=[this]{ if(index>=0 && owner.onSelect) owner.onSelect(index); };
        }
        void paintButton(juce::Graphics& g,bool over,bool down) override
        {
            if(index<0 || index>=owner.rowCount()) return;
            const auto& item=owner.items[static_cast<size_t>(index)];
            g.fillAll(BG);
            if(over || down || hasKeyboardFocus(false))
            { g.setColour(COL_SPECTRUM_DELTA); g.drawRect(getLocalBounds(),1); }
            layout(item.excerpt,item.font,item.colour,getWidth()-8).draw(g,getLocalBounds().reduced(4,3).toFloat());
        }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if(key==juce::KeyPress::upKey || key==juce::KeyPress::downKey)
            { owner.focusRow(index+(key==juce::KeyPress::upKey?-1:1)); return true; }
            if(key==juce::KeyPress::returnKey || key==juce::KeyPress::spaceKey)
            {
                // The base Button may post a delayed click to a row reused after scrolling.
                if(hasKeyboardFocus(false) && isShowing() && index>=0 && owner.onSelect) owner.onSelect(index);
                return true;
            }
            return juce::Button::keyPressed(key);
        }
        ChangeNoteRows& owner;
        int index=-1;
        juce::String noteId;
    };
    void focusRow(int index)
    {
        if(index<0 || index>=rowCount()) return;
        const auto bounds=rowBounds(index);
        if(bounds.getY()<getViewPositionY()) setViewPosition(0,bounds.getY());
        else if(bounds.getBottom()>getViewPositionY()+getViewHeight())
            setViewPosition(0,bounds.getBottom()-getViewHeight());
        for(const auto& row:pool) if(row->isVisible() && row->index==index) { row->grabKeyboardFocus(); break; }
    }
    void refreshButtons()
    {
        const auto top=getViewPositionY(), bottom=top+getViewHeight();
        int index=static_cast<int>(std::upper_bound(offsets.begin(),offsets.end(),top)-offsets.begin())-1;
        Row* previousFocus=nullptr;
        for(const auto& row:pool) if(row->hasKeyboardFocus(false)) {previousFocus=row.get();break;}
        const auto focusedId=previousFocus!=nullptr ? previousFocus->noteId : juce::String{};
        Row* nextFocus=nullptr;
        size_t used=0;
        for(index=juce::jmax(0,index); index<rowCount() && offsets[static_cast<size_t>(index)]<bottom;++index)
        {
            if(used==pool.size())
            {
                pool.push_back(std::make_unique<Row>(*this));
                content.addChildComponent(*pool.back());
            }
            auto& row=*pool[used++];
            row.index=index;
            row.noteId=items[static_cast<size_t>(index)].noteId;
            if(previousFocus!=nullptr && focusedId.isNotEmpty() && row.noteId==focusedId) nextFocus=&row;
            row.setTitle(excerpt(index)); row.setDescription(excerpt(index));
            row.setBounds(rowBounds(index)); row.setVisible(true); row.repaint();
        }
        if(previousFocus!=nullptr && nextFocus!=previousFocus)
        {
            if(nextFocus!=nullptr && nextFocus->isShowing()) nextFocus->grabKeyboardFocus();
            else if(isShowing()) grabKeyboardFocus();
            if(previousFocus->hasKeyboardFocus(false)) previousFocus->giveAwayKeyboardFocus();
        }
        for(;used<pool.size();++used)
        {
            auto& row=*pool[used];
            row.index=-1; row.noteId.clear();
            row.setTitle({}); row.setDescription({});
            row.setVisible(false);
        }
    }
    juce::Component content;
    std::vector<Item> items;
    std::vector<int> offsets {0};
    std::vector<std::unique_ptr<Row>> pool;
};
}
