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
    ReferenceChoice version, check, reference;  // reference: H8 の B（REF）の曲
    bool versionAuto = false;                   // H7: V の Version は AUTO が選んだ（AUTO が選び直せる）
    juce::String songSetId;                     // H8: 選んでいる B SET
    VisualViewChoice visualView;
    int viewedSlot = 2;
    // 2026-10-04：A の取り込み（ACapture）と Tonal の表示（ReferenceTonal）は書かず、古い版が残したものは読み飛ばす。
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
        append ("B", version); append ("C", check); append ("REF", reference); visualView.write (*xml);
        if (safeId (songSetId)) xml->getChildByName ("REF")->setAttribute ("set", songSetId);
        if (versionAuto && version.candidateId.isNotEmpty()) xml->getChildByName ("B")->setAttribute ("auto", true);
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
        result.visualView = VisualViewChoice::read (*xml);
        result.version = readChoice ("B"); result.check = readChoice ("C"); result.reference = readChoice ("REF");
        if (result.version.candidateId.isEmpty()) result.version = {};
        if (const auto* versionXml = xml->getChildByName ("B"))
            result.versionAuto = result.version.candidateId.isNotEmpty() && versionXml->getBoolAttribute ("auto");
        if (result.reference.candidateId.isEmpty()) result.reference = {};
        if (const auto* songs = xml->getChildByName ("REF"))
            if (safeId (songs->getStringAttribute ("set"))) result.songSetId = songs->getStringAttribute ("set");
        const auto viewed = xml->getIntAttribute ("viewed_slot", 2);
        result.viewedSlot = viewed == 1 || viewed == 3 ? viewed : 2;
        return result;
    }
};
}
