#pragma once
#include "ReferenceAuditionProtocol.h"
#include "ReferencePersistedState.h"
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
    ReferenceChoice version, check, reference;  // reference: H8 の B（REF）の曲
    juce::String songSetId;                     // H8: 選んでいる B SET
    VisualViewChoice visualView;
    int viewedSlot = 2;
    juce::String captureState; bool capturedView=false;
    TonalDisplayState tonal;
    WorkflowResumeState workflow;
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
        if(captureState.isNotEmpty() && captureState.getNumBytesAsUTF8() <= referenceCaptureMaximumEncodedBytes)
        { auto* captured=xml->createNewChildElement("ACapture"); captured->setAttribute("data",captureState); captured->setAttribute("shown",capturedView); }
        append ("B", version); append ("C", check); append ("REF", reference); visualView.write (*xml);
        if (safeId (songSetId)) xml->getChildByName ("REF")->setAttribute ("set", songSetId);
        tonal.write (parent); workflow.write (parent);
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
        if(const auto* captured=xml->getChildByName("ACapture")) { const auto data=captured->getStringAttribute("data"); if(data.getNumBytesAsUTF8()<=referenceCaptureMaximumEncodedBytes) result.captureState=data; result.capturedView=captured->getBoolAttribute("shown"); }
        result.visualView = VisualViewChoice::read (*xml);
        result.tonal = TonalDisplayState::read (parent);
        result.workflow = WorkflowResumeState::read (parent);
        result.version = readChoice ("B"); result.check = readChoice ("C"); result.reference = readChoice ("REF");
        if (result.version.candidateId.isEmpty()) result.version = {};
        if (result.reference.candidateId.isEmpty()) result.reference = {};
        if (const auto* songs = xml->getChildByName ("REF"))
            if (safeId (songs->getStringAttribute ("set"))) result.songSetId = songs->getStringAttribute ("set");
        const auto viewed = xml->getIntAttribute ("viewed_slot", 2);
        result.viewedSlot = viewed == 1 || viewed == 3 ? viewed : 2;
        return result;
    }
};
}
