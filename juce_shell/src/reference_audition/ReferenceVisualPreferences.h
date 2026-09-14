#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
namespace hypha::reference_audition
{
struct VisualViewChoice
{
    juce::String sourceHash;
    double start = 0, end = 12;
    bool follow = true, crest = false;
    bool valid() const noexcept
    { return sourceHash.length() == 64 && sourceHash.containsOnly ("0123456789abcdef")
        && std::isfinite (start) && std::isfinite (end) && start >= 0 && end > start && end <= 86400; }
    void write (juce::XmlElement& parent) const
    {
        if (!valid()) return;
        auto* view = parent.createNewChildElement ("View");
        view->setAttribute ("source", sourceHash); view->setAttribute ("start", start); view->setAttribute ("end", end);
        view->setAttribute ("follow", follow); view->setAttribute ("crest", crest);
    }
    static VisualViewChoice read (const juce::XmlElement& parent)
    {
        VisualViewChoice result;
        if (const auto* view = parent.getChildByName ("View"))
            result = { view->getStringAttribute ("source"), view->getDoubleAttribute ("start"),
                view->getDoubleAttribute ("end"), view->getBoolAttribute ("follow", true), view->getBoolAttribute ("crest") };
        return result.valid() ? result : VisualViewChoice {};
    }
};
// Non-RT preferences survive editor close; source observations never do.
class VisualPreferences final
{
public:
    VisualViewChoice get() const { const juce::ScopedLock lock (mutex); return choice; }
    void set (VisualViewChoice value) { const juce::ScopedLock lock (mutex); choice = value.valid() ? std::move (value) : VisualViewChoice {}; }
private:
    mutable juce::CriticalSection mutex;
    VisualViewChoice choice;
};
}
