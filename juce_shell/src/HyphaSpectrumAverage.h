#pragma once

#include <array>
#include <atomic>
#include <juce_core/juce_core.h>

namespace hypha::spectrum_average
{
// A time constant for the live drawing in dB, independent of the measurement aperture.
// LIVE retains the established attack/release; the other choices follow both signs equally.
enum class Choice { live, ms200, seconds1, seconds3, seconds10 };
inline constexpr std::array<Choice, 5> choices { Choice::live, Choice::ms200, Choice::seconds1,
                                               Choice::seconds3, Choice::seconds10 };
inline bool valid (int value) noexcept { return value >= 0 && value < static_cast<int> (choices.size()); }
inline double milliseconds (Choice choice) noexcept
{
    switch (choice)
    {
        case Choice::ms200: return 200.0;
        case Choice::seconds1: return 1'000.0;
        case Choice::seconds3: return 3'000.0;
        case Choice::seconds10: return 10'000.0;
        case Choice::live: return 0.0;
    }
    return 0.0;
}
inline const char* label (Choice choice) noexcept
{
    switch (choice)
    {
        case Choice::ms200: return "0.2 S";
        case Choice::seconds1: return "1 S";
        case Choice::seconds3: return "3 S";
        case Choice::seconds10: return "10 S";
        case Choice::live: return "LIVE";
    }
    return "LIVE";
}
class Preference final
{
public:
    Choice get() const noexcept { return choice.load (std::memory_order_acquire); }
    bool set (Choice next) noexcept
    {
        if (! valid (static_cast<int> (next))) next = Choice::live;
        return choice.exchange (next, std::memory_order_acq_rel) != next;
    }
    void write (juce::XmlElement& xml) const
    {
        const auto value = get();
        if (value != Choice::live) xml.setAttribute ("freq_curve_average", label (value));
    }
    static Choice read (const juce::XmlElement& xml)
    {
        const auto value = xml.getStringAttribute ("freq_curve_average");
        for (auto option : choices) if (value == label (option)) return option;
        return Choice::live;
    }
private:
    std::atomic<Choice> choice { Choice::live };
};
}
