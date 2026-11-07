#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace hypha::live_compare
{
struct ReaperCandidateName
{
    juce::String instance, name;
    int position = -1;
};
// Read only our public saved-state identity from the documented vst_chunk. Wrapper prefixes
// are not interpreted. Missing/truncated/ambiguous state leaves the order unknown.
// https://www.reaper.fm/sdk/reascript/reascripthelp.html#TrackFX_GetNamedConfigParm
inline juce::String candidateIdentityFromChunk (const juce::String& encoded)
{
    if (encoded.isEmpty()) return {};
    juce::MemoryOutputStream bytes;
    if (!juce::Base64::convertFromBase64 (bytes, encoded) || bytes.getDataSize() > 262144) return {};
    const std::string data (static_cast<const char*> (bytes.getData()), bytes.getDataSize());
    const auto begin = data.find ("<KirinHyphaState ");
    if (begin == std::string::npos || data.find ("<KirinHyphaState ", begin + 1) != std::string::npos) return {};
    const auto tagEnd = data.find ('>', begin);
    if (tagEnd == std::string::npos) return {};
    const auto end = data[tagEnd - 1] == '/' ? tagEnd + 1 : data.find ("</KirinHyphaState>", tagEnd);
    if (end == std::string::npos) return {};
    const auto length = data[tagEnd - 1] == '/' ? end - begin : end + std::strlen ("</KirinHyphaState>") - begin;
    const auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data.data() + begin, static_cast<int> (length)));
    return xml && xml->hasTagName ("KirinHyphaState") ? xml->getStringAttribute ("instance_id") : juce::String();
}

template<class Candidates>
void describeCandidateNames (Candidates& candidates, const std::vector<ReaperCandidateName>& names)
{
    for (auto& candidate : candidates)
    {
        const ReaperCandidateName* found = nullptr;
        int count = 0;
        for (const auto& name : names) if (name.instance == candidate.instanceId) { found = &name; ++count; }
        if (count == 1) { candidate.hostName = found->name; candidate.chainPosition = found->position; }
    }
    std::stable_sort (candidates.begin(), candidates.end(), [] (const auto& a, const auto& b)
    {
        if (a.chainPosition < 0) return false;
        return b.chainPosition < 0 || a.chainPosition < b.chainPosition;
    });
}
}
