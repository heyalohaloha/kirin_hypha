#pragma once

namespace hypha::os_access
{
enum class State
{
    unowned,
    ownedDisconnected,
    connectedUnprepared,
    ready,
};

constexpr State classify (bool osOwned, bool connected, bool prepared) noexcept
{
    return ! osOwned ? State::unowned
         : ! connected ? State::ownedDisconnected
         : prepared ? State::ready : State::connectedUnprepared;
}

constexpr bool tabEnabled (State state) noexcept
{
    return state != State::unowned;
}

constexpr bool featureReady (State state) noexcept
{
    return state == State::ready;
}

static_assert (classify (false, false, false) == State::unowned);
static_assert (classify (false, true, true) == State::unowned);
static_assert (classify (true, false, false) == State::ownedDisconnected);
static_assert (classify (true, true, false) == State::connectedUnprepared);
static_assert (classify (true, true, true) == State::ready);
static_assert (! tabEnabled (State::unowned));
static_assert (tabEnabled (State::ownedDisconnected));
static_assert (! featureReady (State::connectedUnprepared));
static_assert (featureReady (State::ready));
}
