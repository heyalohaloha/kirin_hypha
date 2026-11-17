#include "HyphaLuFace.h"

#include "HyphaKeyLight.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

#include <cmath>

namespace hypha::hybrid_vu
{
// A plate like the row's other controls, with its label never below 12 px.
class LuFace::Control final : public juce::Button
{
public:
    explicit Control (const juce::String& text) : juce::Button (text)
    {
        setButtonText (text);
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setPresentationContext (presentation::Context next)
    {
        next.referenceCompact = true; // the 12 px floor of the Reference pages, at every editor size
        if (context != next) { context = next; repaint(); }
    }
    float fontHeight() const { return labelFont (context, typography::TextRole::action).getHeight(); }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const key_light::Scope light (*this);
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        surface_material::paintControl (g, area, highlighted, down, getToggleState());
        g.setColour (getToggleState() ? COL_FLORA_BR : highlighted ? COL_NORMAL : COL_TEXT_SECONDARY);
        g.setFont (labelFont (context, typography::TextRole::action));
        text_style::drawText (g, getButtonText(), getLocalBounds().toFloat(), juce::Justification::centred,
                              false, text_style::LabelPolicy::fixed);
        if (hasKeyboardFocus (true))
        {
            g.setColour (COL_SPECTRUM_DELTA_BR);
            g.drawRoundedRectangle (area.reduced (1.0f), 3.0f, 1.0f);
        }
    }

private:
    presentation::Context context = presentation::defaultContext();
};

LuFace::LuFace (juce::Component& ownerIn)
    : owner (ownerIn), faceButton (std::make_unique<Control> ("LU")),
      windowButton (std::make_unique<Control> ("M"))
{
    faceButton->setComponentID ("observatory-lu-face");
    faceButton->setTitle ("LU face");
    faceButton->setTooltip ("Read loudness in LU against the Reference song chosen in B or C");
    faceButton->onClick = [this] { choose (! luSelected, chosenWindow); };
    windowButton->setComponentID ("observatory-lu-window");
    windowButton->setTitle ("LU window");
    windowButton->setTooltip ("Switch Momentary / Short-term / Integrated");
    windowButton->onClick = [this] { choose (luSelected, lu_needle::nextWindow (chosenWindow)); };
    owner.addChildComponent (*faceButton);
    owner.addChildComponent (*windowButton);
}

LuFace::~LuFace() { stopTimer(); }

juce::Button& LuFace::faceControl() noexcept { return *faceButton; }
juce::Button& LuFace::windowControl() noexcept { return *windowButton; }
float LuFace::controlFontHeightForTest() const { return juce::jmin (faceButton->fontHeight(), windowButton->fontHeight()); }

void LuFace::restore (bool luFace, lu_needle::Window window)
{
    if (luSelected == luFace && chosenWindow == window) return;
    luSelected = luFace;
    chosenWindow = window;
    snapNext = true;
    windowButton->setButtonText (lu_needle::windowLabel (chosenWindow));
    faceButton->setToggleState (luSelected, juce::dontSendNotification);
    if (onLayoutChange) onLayoutChange();
}

void LuFace::choose (bool luFace, lu_needle::Window window)
{
    restore (luFace, window);
    if (onChoice) onChoice (luSelected, chosenWindow);
}

void LuFace::setFacts (const lu_needle::Facts& next)
{
    if (current == next) return;
    const bool layoutChanged = current.readable != next.readable;
    // A different song or a different 0 is a new reading, not a move of the old one.
    if (current.zero != next.zero || current.song != next.song || current.playedZero() != next.playedZero())
        snapNext = true;
    current = next;
    if (layoutChanged) { snapNext = true; if (onLayoutChange) onLayoutChange(); }
    else if (active()) owner.repaint();
}

void LuFace::hideControls()
{
    faceButton->setVisible (false);
    windowButton->setVisible (false);
    stopTimer();
    snapNext = true;
}

void LuFace::layout (juce::Rectangle<int>& row, int buttonHeight, const presentation::Context& context)
{
    faceButton->setPresentationContext (context);
    windowButton->setPresentationContext (context);
    faceButton->setToggleState (active(), juce::dontSendNotification);
    faceButton->setVisible (available());
    windowButton->setVisible (active());
    if (! active()) { stopTimer(); snapNext = true; }
    if (! available()) return;
    const auto place = [&row, buttonHeight] (juce::Button& button, int width)
    {
        row.removeFromLeft (4);
        button.setBounds (row.removeFromLeft (width).withSizeKeepingCentre (width, buttonHeight));
    };
    place (*faceButton, juce::jlimit (34, 72, juce::roundToInt (owner.getWidth() * 0.08f)));
    if (active()) place (*windowButton, juce::jlimit (28, 56, juce::roundToInt (owner.getWidth() * 0.065f)));
}

lu_face::Frame LuFace::frame (const State& state, const KirinDelta& delta, bool deltaAvailable) const
{
    using lu_needle::Window;
    const auto ownLufs = chosenWindow == Window::integrated
        ? (state.cumulativeAvailable ? state.meter.lufs_i : lu_needle::nan)
        : ! state.currentAvailable ? lu_needle::nan
        : chosenWindow == Window::shortTerm ? state.meter.lufs_s : state.meter.lufs_m;
    // The PRE of the same moment exists only where POST confirmed it; it is never made up.
    const auto postMinusPre = state.role == observatory::Role::post && deltaAvailable && chosenWindow != Window::integrated
        ? (chosenWindow == Window::shortTerm ? delta.lufs_s : delta.lufs) : lu_needle::nan;
    lu_face::Frame result { state, current, chosenWindow, lu_needle::reading (current, chosenWindow, ownLufs, postMinusPre) };
    result.own = lu_needle::normalized (result.reading.own);
    result.pre = lu_needle::normalized (result.reading.pre);
    result.samePlace = lu_needle::normalized (result.reading.samePlace);
    return result;
}

bool LuFace::step (double seconds)
{
    const bool a = own.step (ownTarget, seconds);
    const bool b = preShown && pre.step (preTarget, seconds);
    const bool c = samePlaceShown && samePlace.step (samePlaceTarget, seconds);
    return a || b || c;
}

void LuFace::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const bool moving = step (juce::jlimit (0.0, 0.25, (now - lastStepMs) / 1000.0));
    lastStepMs = now;
    if (! moving || ! active() || ! owner.isShowing()) stopTimer();
    owner.repaint (lu_face::dialBounds (lastBounds));
}

void LuFace::paint (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state, const KirinDelta& delta,
                    bool deltaAvailable, float pixelScale)
{
    if (bounds.isEmpty()) return;
    auto vu = state; // the first value cell follows the LU window where it can (M or S)
    vu.shortTermLoudness = chosenWindow == lu_needle::Window::shortTerm
        || (chosenWindow == lu_needle::Window::integrated && state.shortTermLoudness);
    auto shown = frame (vu, delta, deltaAvailable);
    const bool hadPre = preShown, hadSamePlace = samePlaceShown;
    ownTarget = shown.own; preTarget = shown.pre; samePlaceTarget = shown.samePlace;
    preShown = std::isfinite (shown.reading.pre);
    samePlaceShown = std::isfinite (shown.reading.samePlace);
    // Offscreen (a capture, a test) there is no frame clock: the needles show the values themselves.
    const bool live = owner.isShowing() && owner.getPeer() != nullptr;
    if (snapNext || ! live) { own.snap (ownTarget); pre.snap (preTarget); samePlace.snap (samePlaceTarget); }
    if (! hadPre) pre.snap (preTarget);
    if (! hadSamePlace) samePlace.snap (samePlaceTarget);
    snapNext = false;
    lastBounds = bounds;
    if (live && ! isTimerRunning()
        && (own.shown != ownTarget || (preShown && pre.shown != preTarget) || (samePlaceShown && samePlace.shown != samePlaceTarget)))
    {
        lastStepMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (30);
    }
    shown.own = own.shown; shown.pre = pre.shown; shown.samePlace = samePlace.shown;

    pixelScale = std::isfinite (pixelScale) ? juce::jlimit (1.0f, 4.0f, pixelScale) : 1.0f;
    const juce::Point<int> size (bounds.getWidth(), bounds.getHeight());
    const juce::Rectangle<int> local (0, 0, size.x, size.y);
    if (! backdrop.isValid() || backdropSize != size || backdropScale != pixelScale || backdropRole != state.role
        || backdropPresentation != state.presentation || backdropJungle != state.jungle)
    {
        backdropSize = size; backdropScale = pixelScale; backdropRole = state.role;
        backdropPresentation = state.presentation; backdropJungle = state.jungle;
        backdrop = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (size.x * pixelScale)),
                                juce::jmax (1, juce::roundToInt (size.y * pixelScale)), true);
        juce::Graphics layer (backdrop);
        layer.addTransform (juce::AffineTransform::scale (pixelScale));
        paintStatic (layer, local, vu);
    }
    g.drawImage (backdrop, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
    const juce::Graphics::ScopedSaveState saved (g);
    g.setOrigin (bounds.getPosition());
    paintDynamic (g, local, vu);
    drawnText.clear();
    lu_face::paintScale (g, local, vu, &drawnText);
    lu_face::paintLive (g, local, shown, &drawnText);
}
}
