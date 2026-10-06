#pragma once

#include "HyphaBlindScreen.h"
#include "live_compare/LiveCompareProcessorState.h"

namespace hypha::live_blind_ui
{
// PRE/POST の LIVE BLIND。見た目と置き方は VERSION BLIND と共通（HyphaBlindScreen.h）で、ここは中身だけを作る。
blind_ui::Screen screenFor (const live_compare::LiveBlindStatus&, bool playing, float postActual);

class Component final : public blind_ui::ScreenComponent
{
public:
    Component() : blind_ui::ScreenComponent ("live-blind") {}
    void setState (const live_compare::LiveBlindStatus& next, bool hostPlaying, float postActual)
    {
        current = next;
        setScreen (screenFor (next, hostPlaying, postActual));
    }
    const live_compare::LiveBlindStatus& state() const noexcept { return current; }

private:
    live_compare::LiveBlindStatus current;
};
}
