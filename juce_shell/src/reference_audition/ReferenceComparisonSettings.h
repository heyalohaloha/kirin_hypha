#pragma once
#include "ReferenceAuditionProtocol.h"
#include "ReferenceVisualPreferences.h"

namespace hypha::reference_audition
{
struct ReferenceChoice
{
    juce::String presetId, checkId, candidateId, cueId;
    bool valid() const
    {
        for (const auto* id : { &presetId, &checkId, &candidateId, &cueId })
            if (id->length() > 160 || (id->isNotEmpty() && ! safeId (*id))) return false;
        return (checkId.isEmpty() || presetId.isNotEmpty())
            && (candidateId.isEmpty() || checkId.isNotEmpty())
            && (cueId.isEmpty() || candidateId.isNotEmpty());
    }
    juce::String target() const { return presetId + "/" + checkId + "/" + candidateId; }
};

struct ReferenceComparisonSettings
{
    ReferenceChoice version, check;
    VisualViewChoice visualView;
    int viewedSlot = 2;
    juce::String captureState; bool capturedView=false;
    void write (juce::XmlElement& parent) const
    {
        auto* xml = parent.createNewChildElement ("ReferenceChoices");
        xml->setAttribute ("version", 1);
        xml->setAttribute ("viewed_slot", viewedSlot);
        const auto append = [xml] (const char* name, const ReferenceChoice& choice) {
            auto* child = xml->createNewChildElement (name);
            child->setAttribute ("preset", choice.presetId);
            child->setAttribute ("check", choice.checkId);
            child->setAttribute ("candidate", choice.candidateId);
            child->setAttribute ("cue", choice.cueId);
        };
        if(captureState.isNotEmpty()) { auto* captured=xml->createNewChildElement("ACapture"); captured->setAttribute("data",captureState); captured->setAttribute("shown",capturedView); }
        append ("B", version); append ("C", check); visualView.write (*xml);
    }
    static ReferenceComparisonSettings read (const juce::XmlElement& parent)
    {
        ReferenceComparisonSettings result;
        const auto* xml = parent.getChildByName ("ReferenceChoices");
        if (xml == nullptr || xml->getIntAttribute ("version") != 1) return result;
        const auto readChoice = [xml] (const char* name) {
            ReferenceChoice choice;
            if (const auto* child = xml->getChildByName (name))
                choice = { child->getStringAttribute ("preset"), child->getStringAttribute ("check"),
                           child->getStringAttribute ("candidate"), child->getStringAttribute ("cue") };
            return choice.valid() ? choice : ReferenceChoice {};
        };
        if(const auto* captured=xml->getChildByName("ACapture")) { const auto data=captured->getStringAttribute("data"); if(data.getNumBytesAsUTF8()<=1024*1024-4096) result.captureState=data; result.capturedView=captured->getBoolAttribute("shown"); }
        result.visualView = VisualViewChoice::read (*xml);
        result.version = readChoice ("B"); result.check = readChoice ("C");
        if (result.version.candidateId.isEmpty()) result.version = {};
        result.viewedSlot = xml->getIntAttribute ("viewed_slot", 2) == 1 ? 1 : 2;
        return result;
    }
};
}
