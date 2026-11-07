#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaShortcutText.h"
#include "HyphaTextButton.h"

// KEYS: every key of the window, built from the table (HyphaShortcutText.h). What works now comes
// first; at any size the list scrolls and nothing is cut. It changes nothing: opening it does not touch
// LISTEN, BLIND, Reference or a gain, and it is not saved. The editor owns when it opens and closes.
namespace hypha
{
class KeysPanel final : public juce::Component
{
public:
    KeysPanel();
    std::function<void()> onClose;
    void setRows (std::vector<shortcuts::ListRow>);
    const std::vector<shortcuts::ListRow>& rows() const noexcept { return shownRows; }
    void focusList();
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;

private:
    class List final : public juce::Component
    {
    public:
        explicit List (const std::vector<shortcuts::ListRow>& rowsIn) : rows (rowsIn) {}
        void setContext (presentation::Context next) { context = next; }
        int rowHeight() const noexcept;
        void paint (juce::Graphics&) override;

    private:
        const std::vector<shortcuts::ListRow>& rows;
        presentation::Context context = presentation::defaultContext();
    };

    std::vector<shortcuts::ListRow> shownRows;
    List list { shownRows };
    juce::Viewport viewport;
    HyphaTextButton closeButton { "CLOSE" };
    juce::Label title;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysPanel)
};
}
