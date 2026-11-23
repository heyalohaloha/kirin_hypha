#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
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
    std::function<void(const juce::String&)> onSelect;
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
        }
        void bind(int nextIndex,const juce::String& id)
        {
            index=nextIndex;
            if(noteId==id) return;
            noteId=id; ++generation;
            invalidateAccessibilityHandler();
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
            const bool isReturn=key.isKeyCode(juce::KeyPress::returnKey);
            if(isReturn || key==juce::KeyPress::spaceKey)
            {
                // Match Button's Return modifiers without its delayed, unbound triggerClick.
                const auto mods=key.getModifiers().withoutMouseButtons().getRawFlags();
                if(hasKeyboardFocus(false) && (isReturn || mods==0 || mods==juce::ModifierKeys::shiftModifier))
                    activate(binding());
                return true;
            }
            return false;
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            mouseBinding=binding();
            juce::Button::mouseDown(event);
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            const auto pressed=std::exchange(mouseBinding,std::nullopt);
            const bool releasedInside=isDown() && isOver() && getLocalBounds().contains(event.getPosition());
            juce::Button::mouseUp(event); // updates native button state; no unbound onClick is installed
            if(releasedInside && pressed) activate(*pressed);
        }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            const auto safe=juce::Component::SafePointer<Row>(this);
            const auto pressed=binding();
            return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::button,
                juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,[safe,pressed]{
                    juce::MessageManager::callAsync([safe,pressed]{
                        if(auto* row=safe.getComponent()) row->activate(pressed);
                    });
                }));
        }
        ChangeNoteRows& owner;
        int index=-1;
        juce::String noteId;
    private:
        struct Binding { juce::String id; std::uint64_t revision; };
        Binding binding() const { return {noteId,generation}; }
        void activate(const Binding& pressed)
        {
            if(!isShowing() || !isEnabled() || pressed.id.isEmpty() || pressed.revision!=generation || pressed.id!=noteId
                || index<0 || index>=owner.rowCount() || owner.items[static_cast<size_t>(index)].noteId!=pressed.id) return;
            if(owner.onSelect) owner.onSelect(pressed.id);
        }
        std::optional<Binding> mouseBinding;
        std::uint64_t generation=0;
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
            row.bind(index,items[static_cast<size_t>(index)].noteId);
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
            row.bind(-1,{});
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
