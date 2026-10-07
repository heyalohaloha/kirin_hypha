// Included inside the product fixture's anonymous namespace, after Processor and require.
juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* child = find (*parent.getChildComponent (i), id)) return child;
    return nullptr;
}

const hypha::observatory::View* findView (juce::Component& parent)
{
    if (auto* view = dynamic_cast<const hypha::observatory::View*> (&parent)) return view;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* view = findView (*parent.getChildComponent (i))) return view;
    return nullptr;
}

// Exercise the real END button with NO audio callback: dismissal cannot depend on rendering.
// RT receipts and the approved attenuation stay untouched until the host supplies audio again.
class SuspendedBlindEndProbe
{
public:
    bool ready (std::atomic<bool>& suspend, const std::atomic<bool>& suspended)
    {
        armed = true;
        suspend.store (true);
        return suspended.load();
    }
    void accepted (Processor& processor, juce::Component& editor)
    {
        const auto state = processor.liveCompareStatus();
        require (! state.active && state.finishing, "END closes comparison without claiming audio completion");
        require (! find (editor, "live-blind-screen")->isVisible(), "one END closes Blind without any callback");
        require (findView (editor)->isAccessible() && findView (editor)->isEnabled(),
                 "normal controls and accessibility return immediately on END");
        actual = state.postActual;
        acceptedAt = std::chrono::steady_clock::now();
    }
    bool resume (Processor& processor, juce::Component& editor, std::atomic<bool>& suspend)
    {
        if (! armed) return true; // fault scenarios have their own lifecycle assertions
        if (std::chrono::steady_clock::now() - acceptedAt < std::chrono::milliseconds (300)) return false;
        const auto state = processor.liveCompareStatus();
        require (! state.active && state.finishing && std::abs (state.postActual - actual) <= 0.0f,
                 "message-thread ticks never release attenuation or fake an audio receipt");
        require (processor.liveCompareAdmission (false) == hypha::live_compare::StartResult::returnPending,
                 "new entry identifies the pending return, not another END operation");
        // The message thread posts the note; on a loaded machine it can follow the state by a moment.
        if (! findView (editor)->feedback().contains ("Ended;")
            && std::chrono::steady_clock::now() - acceptedAt < std::chrono::seconds (2))
            return false;
        require (findView (editor)->feedback().contains ("Ended;"), "pending audio return remains visible");
        require (! find (editor, "observatory-live-end")->isVisible()
            && ! find (editor, "observatory-live-pre")->isVisible(), "closed comparison has no stale controls");
        suspend.store (false);
        armed = false;
        return true;
    }
private:
    bool armed = false;
    float actual = 1.0f;
    std::chrono::steady_clock::time_point acceptedAt;
};
