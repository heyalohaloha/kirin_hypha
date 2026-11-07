#pragma once
#include "ReferenceKirinSpectrum.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>

namespace hypha::reference_audition
{
// Project-local facts only. No audio, source path, decoder, or playback identity belongs here.
struct MeasuredTarget
{
    juce::String id, name;
    KirinSpectrumWindow spectrum;
    int channels = 2; // Earlier numeric states used the stereo comparison convention.
    double integrated = std::numeric_limits<double>::quiet_NaN(), maximumTruePeak = -300.0, duration = 0.0;
    bool valid() const
    {
        if (! id.startsWith ("target-") || id.length() > 80 || ! id.containsOnly ("target-0123456789abcdef")
            || (channels != 1 && channels != 2) || name.isEmpty() || name.length() > 120 || ! std::isfinite (duration) || duration <= 0.0
            || ! std::isfinite (maximumTruePeak) || maximumTruePeak < -300.0 || maximumTruePeak > 100.0
            || (! std::isnan (integrated) && (! std::isfinite (integrated) || integrated < -300.0 || integrated > 100.0))) return false;
        for (const auto* values : { &spectrum.p10Db, &spectrum.medianDb, &spectrum.p90Db })
            if (values->size() != 64) return false;
        for (size_t i = 0; i < 64; ++i)
            if (! std::isfinite (spectrum.p10Db[i]) || ! std::isfinite (spectrum.p90Db[i])
                || ! std::isfinite (spectrum.medianDb[i]) || spectrum.p10Db[i] < -300.0f || spectrum.p90Db[i] > 100.0f
                || spectrum.p10Db[i] > spectrum.medianDb[i] || spectrum.medianDb[i] > spectrum.p90Db[i]) return false;
        for (const auto value : spectrum.balanceDb)
            if (! std::isfinite (value) || value < -300.0 || value > 100.0) return false;
        return true;
    }
    void setCenters()
    {
        spectrum.centersHz.resize (64);
        for (size_t i = 0; i < 64; ++i) spectrum.centersHz[i] = 20.0 * std::pow (1000.0, double (i) / 63.0);
    }
    template <typename Values> static juce::String numbers (const Values& values)
    {
        juce::StringArray text;
        for (const auto value : values) text.add (juce::String (double (value), 17));
        return text.joinIntoString (" ");
    }
    template <typename Values> static bool readNumbers (const juce::String& text, Values& values, size_t count)
    {
        if (text.length() > 5000) return false;
        const auto parts = juce::StringArray::fromTokens (text, " ", "");
        if (parts.size() != static_cast<int> (count)) return false;
        for (size_t i = 0; i < count; ++i)
        {
            std::istringstream parser (parts[static_cast<int> (i)].toStdString());
            parser.imbue (std::locale::classic());
            double value = 0.0;
            if (! (parser >> value) || ! parser.eof() || ! std::isfinite (value)) return false;
            values[i] = static_cast<typename Values::value_type> (value);
        }
        return true;
    }
    void write (juce::XmlElement& parent) const
    {
        if (! valid()) return;
        auto* xml = parent.createNewChildElement ("Target");
        xml->setAttribute ("id", id); xml->setAttribute ("name", name);
        xml->setAttribute ("channels", channels);
        xml->setAttribute ("duration", duration); xml->setAttribute ("tp", maximumTruePeak);
        if (std::isfinite (integrated)) xml->setAttribute ("integrated", integrated);
        xml->setAttribute ("p10", numbers (spectrum.p10Db)); xml->setAttribute ("median", numbers (spectrum.medianDb));
        xml->setAttribute ("p90", numbers (spectrum.p90Db)); xml->setAttribute ("balance", numbers (spectrum.balanceDb));
    }
    static MeasuredTarget read (const juce::XmlElement& xml)
    {
        MeasuredTarget result;
        result.id = xml.getStringAttribute ("id"); result.name = xml.getStringAttribute ("name");
        const auto scalar = [&xml] (const char* key, double& value) {
            std::array<double, 1> read {};
            if (! readNumbers (xml.getStringAttribute (key), read, 1)) return false;
            value = read[0]; return true;
        };
        if (! scalar ("duration", result.duration) || ! scalar ("tp", result.maximumTruePeak)
            || (xml.hasAttribute ("integrated") && ! scalar ("integrated", result.integrated))) return {};
        if (xml.hasAttribute ("channels"))
        {
            double channels = 0.0;
            if (! scalar ("channels", channels) || (channels != 1.0 && channels != 2.0)) return {};
            result.channels = static_cast<int> (channels);
        }
        for (auto* values : { &result.spectrum.p10Db, &result.spectrum.medianDb, &result.spectrum.p90Db }) values->resize (64);
        if (! readNumbers (xml.getStringAttribute ("p10"), result.spectrum.p10Db, 64)
            || ! readNumbers (xml.getStringAttribute ("median"), result.spectrum.medianDb, 64)
            || ! readNumbers (xml.getStringAttribute ("p90"), result.spectrum.p90Db, 64)
            || ! readNumbers (xml.getStringAttribute ("balance"), result.spectrum.balanceDb, 4)) return {};
        result.setCenters();
        return result.valid() ? result : MeasuredTarget {};
    }
};
struct TargetCollection
{
    static constexpr size_t capacity = 8;
    std::vector<MeasuredTarget> items;
    juce::String selected;
    const MeasuredTarget* find (const juce::String& id) const
    {
        const auto it = std::find_if (items.begin(), items.end(), [&] (const auto& item) { return item.id == id; });
        return it == items.end() ? nullptr : &*it;
    }
    void write (juce::XmlElement& parent) const
    {
        if (items.empty()) return;
        auto* xml = parent.createNewChildElement ("MeasuredTargets");
        xml->setAttribute ("version", 1);
        if (find (selected)) xml->setAttribute ("selected", selected);
        for (size_t i = 0; i < std::min (items.size(), capacity); ++i) items[i].write (*xml);
    }
    static TargetCollection read (const juce::XmlElement& parent)
    {
        TargetCollection result;
        const auto* xml = parent.getChildByName ("MeasuredTargets");
        if (! xml || xml->getIntAttribute ("version") != 1) return result;
        for (const auto* child : xml->getChildIterator())
        {
            if (result.items.size() == capacity) break;
            if (! child->hasTagName ("Target")) continue;
            auto item = MeasuredTarget::read (*child);
            if (item.valid() && ! result.find (item.id)) result.items.push_back (std::move (item));
        }
        const auto selected = xml->getStringAttribute ("selected");
        if (result.find (selected)) result.selected = selected;
        return result;
    }
};
}
