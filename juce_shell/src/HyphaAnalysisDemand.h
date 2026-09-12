#pragma once

#include "HyphaAnalysisNavigation.h"

#include <atomic>
#include <cstdint>

namespace hypha::analysis
{
enum class Kind : std::uint8_t
{
    none = 0,
    spectrum,
    midSideSpectrum,
    psbAbsolute,
    psbDelta,
    sharpAbsolute,
    sharpDelta,
    liveAbsolute,
    attack,
};

struct Demand
{
    Kind kind = Kind::none;
    std::uint8_t channelMode = 0;

    constexpr bool operator== (const Demand& other) const noexcept
    {
        return kind == other.kind && channelMode == other.channelMode;
    }

    constexpr bool operator!= (const Demand& other) const noexcept
    {
        return ! (*this == other);
    }
};

struct SurfaceState
{
    bool presented = false;
    analysis_navigation::Page page = analysis_navigation::Page::meters;
    bool psb = false;
    bool absoluteTarget = true;
    bool midSideSpectrum = false;
    bool sharpAbsolute = false;
    std::uint8_t channelMode = 0;
    bool attackAvailable = true;
};

constexpr Demand forSurface (SurfaceState state) noexcept
{
    using Page = analysis_navigation::Page;
    if (! state.presented || ! analysis_navigation::isAnalysis (state.page))
        return {};
    if (state.page == Page::spectrum)
    {
        if (state.psb)
            return { state.absoluteTarget ? Kind::psbAbsolute : Kind::psbDelta, 0u };
        if (state.midSideSpectrum)
            return { Kind::midSideSpectrum, 0u };
        return { Kind::spectrum, state.channelMode };
    }
    if (state.page == Page::perceptual)
        return { state.sharpAbsolute ? Kind::sharpAbsolute : Kind::sharpDelta,
                 state.channelMode };
    if (state.page == Page::absolute)
        return { Kind::liveAbsolute, 0u };
    if (state.page == Page::attack)
        return state.attackAvailable ? Demand { Kind::attack, 0u } : Demand {};
    return {};
}

constexpr bool usesChannelMode (Kind kind) noexcept
{
    return kind == Kind::spectrum || kind == Kind::sharpAbsolute
        || kind == Kind::sharpDelta;
}

constexpr bool valid (Demand demand) noexcept
{
    return demand.kind <= Kind::attack
        && (usesChannelMode (demand.kind) ? demand.channelMode <= 2u
                                         : demand.channelMode == 0u);
}

constexpr bool active (Demand demand) noexcept
{
    return demand.kind != Kind::none;
}

constexpr bool isAttack (Demand demand) noexcept
{
    return demand.kind == Kind::attack;
}

constexpr std::uint16_t encode (Demand demand) noexcept
{
    return static_cast<std::uint16_t> (demand.kind)
         | static_cast<std::uint16_t> (demand.channelMode << 8u);
}

constexpr Demand decode (std::uint16_t encoded) noexcept
{
    return { static_cast<Kind> (encoded & 0xffu),
             static_cast<std::uint8_t> ((encoded >> 8u) & 0xffu) };
}

class OwnerState
{
public:
    std::uint64_t begin() noexcept
    {
        auto next = serial.fetch_add (1, std::memory_order_acq_rel) + 1;
        if (next == 0)
        {
            next = 1;
            serial.store (next, std::memory_order_release);
        }
        owner.store (next, std::memory_order_release);
        request.store (encode ({}), std::memory_order_release);
        return next;
    }

    bool isCurrent (std::uint64_t candidate) const noexcept
    { return candidate != 0 && owner.load (std::memory_order_acquire) == candidate; }

    bool set (std::uint64_t candidate, Demand demand) noexcept
    {
        if (! isCurrent (candidate) || ! valid (demand)) return false;
        request.store (encode (demand), std::memory_order_release);
        return true;
    }

    bool clear (std::uint64_t candidate) noexcept { return set (candidate, {}); }

    bool end (std::uint64_t candidate) noexcept
    {
        if (! clear (candidate)) return false;
        owner.store (0, std::memory_order_release);
        return true;
    }

    void replaceCurrentRequest (Demand demand) noexcept
    {
        if (currentOwner() != 0 && valid (demand))
            request.store (encode (demand), std::memory_order_release);
    }

    std::uint64_t currentOwner() const noexcept
    { return owner.load (std::memory_order_acquire); }

    Demand requested() const noexcept
    { return decode (request.load (std::memory_order_acquire)); }

private:
    std::atomic<std::uint64_t> serial { 0 };
    std::atomic<std::uint64_t> owner { 0 };
    std::atomic<std::uint16_t> request { 0 };
};

// Processor-side application truth. The editor owns only the requested Demand; an equal request
// is not proof that a newly-created engine has accepted it. All methods are called under the
// processor handle lock, so this state deliberately contains no second set of atomics.
class ApplicationState
{
public:
    void engineCreated() noexcept
    {
        ++engineGeneration;
        if (engineGeneration == 0) ++engineGeneration;
        ready = false;
        appliedGeneration = engineGeneration;
        appliedDemand = {};
        retryCountdown = 0;
    }

    void engineDestroyed() noexcept
    {
        ready = false;
        appliedGeneration = 0;
        appliedDemand = {};
        retryCountdown = 0;
    }

    void engineReady() noexcept
    {
        ready = engineGeneration != 0;
        retryCountdown = 0;
    }

    void requestChanged() noexcept { retryCountdown = 0; }

    bool shouldApply (Demand requested) noexcept
    {
        if (! ready || (appliedGeneration == engineGeneration
                        && appliedDemand == requested))
            return false;
        if (retryCountdown > 0)
        {
            --retryCountdown;
            return false;
        }
        return true;
    }

    Demand previousForCurrentEngine() const noexcept
    {
        return appliedGeneration == engineGeneration ? appliedDemand : Demand {};
    }

    void applicationSucceeded (Demand demand) noexcept
    {
        appliedGeneration = engineGeneration;
        appliedDemand = demand;
        retryCountdown = 0;
    }

    void applicationFailed() noexcept
    {
        appliedGeneration = 0;
        appliedDemand = {};
        // A visible editor services the pending request again. Bound retries so a temporarily
        // occupied process-wide slot does not create control-plane churn every paint tick.
        retryCountdown = 3;
    }

    std::uint64_t generation() const noexcept { return engineGeneration; }
    bool isReady() const noexcept { return ready; }
    bool isApplied (Demand demand) const noexcept
    {
        return ready && appliedGeneration == engineGeneration && appliedDemand == demand;
    }

private:
    std::uint64_t engineGeneration = 0;
    std::uint64_t appliedGeneration = 0;
    Demand appliedDemand {};
    std::uint8_t retryCountdown = 0;
    bool ready = false;
};

// Adapter is deliberately tiny: production binds these calls to the C ABI while tests bind them
// to a recorder. This keeps the semantic mapping independently testable without a second engine.
template <typename Adapter>
bool apply (Demand previous, Demand requested, Adapter& adapter)
{
    if (! valid (requested))
        return false;
    if (previous == requested)
        return true;
    switch (requested.kind)
    {
        case Kind::none:
            return isAttack (previous) ? adapter.setAttackEnabled (false)
                                       : adapter.setSpectrumVisible (false);
        case Kind::spectrum:
            return adapter.setChannelMode (requested.channelMode)
                && adapter.setSpectrumVisible (true);
        case Kind::midSideSpectrum:
            return adapter.setMidSideSpectrumVisible (true);
        case Kind::psbAbsolute:
            return adapter.setChannelMode (0u) && adapter.setAbsoluteVisible (true);
        case Kind::psbDelta:
            return adapter.setChannelMode (0u) && adapter.setPerceptualVisible (true);
        case Kind::sharpAbsolute:
            return adapter.setChannelMode (requested.channelMode)
                && adapter.setAbsoluteVisible (true);
        case Kind::sharpDelta:
            return adapter.setChannelMode (requested.channelMode)
                && adapter.setPerceptualVisible (true);
        case Kind::liveAbsolute:
            return adapter.setAbsoluteVisible (true);
        case Kind::attack:
            return adapter.setAttackEnabled (true);
    }
    return false;
}
}
