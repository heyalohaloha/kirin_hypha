#pragma once

#include <array>
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>

// Project-local presentation choices. Zero is always the established appearance/cadence;
// absent and unknown attributes restore it, just as FREQ AVG does.
namespace hypha::display_settings
{
// New fields are appended: the menu action numbers and the stored keys of the older ones stay.
enum Field { range, tilt, smooth, light, peak, rate, fall, fill, fieldCount };
inline constexpr std::array<const char*, fieldCount> keys {
    "freq_range", "freq_tilt", "freq_smooth", "freq_light", "freq_peak", "level_rate", "freq_fall", "freq_fill" };
inline constexpr std::array<const char*, fieldCount> titles {
    "RANGE", "TILT", "SMOOTH", "LIGHT", "PEAK", "NUM RATE", "FALL", "FILL" };
// Three bits per field: at most eight choices. SMOOTH 5..7 widen from the low width to the high
// width (HyphaSpectrumPresentation.h); PEAK SLOW holds and then falls (HyphaSpectrumPeakFall.h).
inline constexpr int choiceLimit = 8;
inline constexpr std::array<std::array<const char*, choiceLimit>, fieldCount> labels {{
    { "96 dB", "60 dB", "90 dB", "120 dB" },
    { "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" },
    { "STD", "OFF", "1/24 oct", "1/12 oct", "1/6 oct", "1/24-1/6 oct", "1/12-1/3 oct", "1/6-1/2 oct" },
    { "1", "0.7", "1.3" },
    { "ON", "OFF", "SLOW" },
    { "LIVE", "0.2 S", "0.5 S", "1 S" },
    { "40 dB/s", "20 dB/s", "12 dB/s", "6 dB/s" },
    { "STD", "RICH" }
}};
struct Values
{
    std::array<int, fieldCount> selected {};
    int operator[] (Field field) const noexcept { return selected[static_cast<size_t> (field)]; }
    bool operator== (const Values& other) const noexcept { return selected == other.selected; }
    bool operator!= (const Values& other) const noexcept { return ! (*this == other); }
    float rangeDb() const noexcept { return std::array<float, 4> { 96, 60, 90, 120 }[(size_t) (*this)[range]]; }
    float tiltDb() const noexcept { return std::array<float, 4> { 0, 3, 4.5f, 6 }[(size_t) (*this)[tilt]]; }
    float brightness() const noexcept { return std::array<float, 3> { 1, 0.7f, 1.3f }[(size_t) (*this)[light]]; }
    float fallDbPerSecond() const noexcept { return std::array<float, 4> { 40, 20, 12, 6 }[(size_t) (*this)[fall]]; }
    bool slowPeak() const noexcept { return (*this)[peak] == 2; }
    bool richFill() const noexcept { return (*this)[fill] == 1; }
    double numericMs() const noexcept { return std::array<double, 4> { 0, 200, 500, 1000 }[(size_t) (*this)[rate]]; }
};
inline bool valid (int field, int choice) noexcept
{
    return field >= 0 && field < fieldCount && choice >= 0 && choice < choiceLimit
        && labels[(size_t) field][(size_t) choice] != nullptr;
}
class Preference final
{
public:
    Values get() const noexcept
    {
        const auto bits = packed.load (std::memory_order_acquire);
        Values value;
        for (int field = 0; field < fieldCount; ++field)
            value.selected[(size_t) field] = static_cast<int> ((bits >> (field * 3)) & 7u);
        return value;
    }
    bool set (Values value) noexcept
    {
        unsigned bits = 0;
        for (int field = 0; field < fieldCount; ++field)
            if (valid (field, value.selected[(size_t) field]))
                bits |= static_cast<unsigned> (value.selected[(size_t) field]) << (field * 3);
        return packed.exchange (bits, std::memory_order_acq_rel) != bits;
    }
    void write (juce::XmlElement& xml) const
    {
        const auto value = get();
        for (int field = 0; field < fieldCount; ++field)
            if (value.selected[(size_t) field] != 0)
                xml.setAttribute (keys[(size_t) field], labels[(size_t) field][(size_t) value.selected[(size_t) field]]);
    }
    static Values read (const juce::XmlElement& xml)
    {
        Values value;
        for (int field = 0; field < fieldCount; ++field)
            for (int choice = 0; valid (field, choice); ++choice)
                if (xml.getStringAttribute (keys[(size_t) field]) == labels[(size_t) field][(size_t) choice])
                    value.selected[(size_t) field] = choice;
        return value;
    }
private:
    std::atomic<unsigned> packed { 0 };
};
inline constexpr int firstAction = 4000;
static_assert (fieldCount * 3 <= 32, "three bits per field fit one word");
// Choosing the PEAK choice that is already selected starts its line again from the current sound.
inline bool reselectsPeak (Values value, int action) noexcept
{ return action == firstAction + peak * 10 + value[peak]; }
inline bool choose (Values& value, int action) noexcept
{
    const auto field = (action - firstAction) / 10, choice = (action - firstAction) % 10;
    if (action < firstAction || ! valid (field, choice)) return false;
    value.selected[(size_t) field] = choice;
    return true;
}
inline void addMenu (juce::PopupMenu& menu, Values value, bool frequency)
{
    juce::PopupMenu freq;
    for (int field = 0; field < fieldCount; ++field)
    {
        if (! frequency && field != rate) continue;
        juce::PopupMenu options;
        for (int choice = 0; valid (field, choice); ++choice)
            options.addItem (firstAction + field * 10 + choice, labels[(size_t) field][(size_t) choice],
                             true, value.selected[(size_t) field] == choice);
        if (field == rate) menu.addSubMenu ("LEVEL / NUM RATE", options);
        else freq.addSubMenu (titles[(size_t) field], options);
    }
    if (frequency) menu.addSubMenu ("FREQ", freq);
}
}
