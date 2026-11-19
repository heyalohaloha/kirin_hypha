#pragma once
#include "../src/HyphaDisplaySettings.h"
#include "../src/HyphaSpectrumAverage.h"
#include <stdexcept>
#include <vector>
namespace hypha::tests::settings_qa {
constexpr std::array<int, 9> radices {4, 4, 8, 3, 3, 4, 4, 2, 5};
constexpr int population = 184320;
inline void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Tuple {
    display_settings::Values values;
    spectrum_average::Choice average = spectrum_average::Choice::live;
};
inline Tuple decode(int id) {
    require(id >= 0 && id < population, "tuple index outside population");
    Tuple result;
    for (int f = 0; f < 8; ++f) {
        result.values.selected[static_cast<size_t>(f)] = id % radices[static_cast<size_t>(f)];
        id /= radices[static_cast<size_t>(f)];
    }
    result.average = static_cast<spectrum_average::Choice>(id);
    return result;
}
inline int encode(const Tuple& tuple) {
    int id = 0, stride = 1;
    for (int f = 0; f < 8; ++f) {
        const int v = tuple.values.selected[static_cast<size_t>(f)];
        require(display_settings::valid(f, v), "invalid display choice");
        id += stride * v; stride *= radices[static_cast<size_t>(f)];
    }
    require(spectrum_average::valid(static_cast<int>(tuple.average)), "invalid average choice");
    return id + stride * static_cast<int>(tuple.average);
}
inline std::vector<int> singleChoiceIds() {
    for (int f = 0; f < 8; ++f) {
        int actual = 0; while (display_settings::valid(f, actual)) ++actual;
        require(actual == radices[static_cast<size_t>(f)], "display choices changed; recount coverage");
    }
    require(spectrum_average::choices.size() == 5, "average choices changed; recount coverage");
    std::vector<int> ids {0}; int stride = 1;
    for (const auto count : radices) {
        for (int v = 1; v < count; ++v) ids.push_back(v * stride);
        stride *= count;
    }
    require(ids.size() == 29 && stride == population, "tuple schema changed");
    return ids;
}
inline void roundtrip(int id, const Tuple& tuple) {
    require(encode(tuple) == id, "mixed radix is not reversible");
    display_settings::Preference settings;
    spectrum_average::Preference average;
    settings.set(tuple.values); average.set(tuple.average);
    juce::XmlElement xml("SettingsFixture"); settings.write(xml); average.write(xml);
    const auto parsed = juce::parseXML(xml.toString());
    require(parsed != nullptr, "serialized settings XML is invalid");
    require(display_settings::Preference::read(*parsed) == tuple.values,
            "display settings XML roundtrip mismatch");
    require(spectrum_average::Preference::read(*parsed) == tuple.average,
            "average XML roundtrip mismatch");
    require(settings.get() == tuple.values && average.get() == tuple.average,
            "preference input mutated during serialization");
}
}
