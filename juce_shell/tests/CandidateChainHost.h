#pragma once
#include "CandidateMenuHost.h"

namespace hypha::tests
{
// Complete public chain API, shared by the three actual processor adapters. Changes describe
// the host's inserts; they never inject a ring proof or the expected product verdict.
class CandidateChainHost final : public Steinberg::FUnknown
{
public:
    inline static CandidateChainHost* current = nullptr;
    bool removed = false, bypassed = false, sourceRemoved = false, hardwareA = false, unavailable = false, postOnly = false;
    int delay = 510;
    int fill = -1, reported = -1; // REAPER's live chain value and an insert's own report, when they differ from delay
    std::array<std::array<unsigned char, 16>, 6> ids {};
    CandidateChainHost()
    { for (unsigned i = 0; i < ids.size(); ++i) ids[i][0] = static_cast<unsigned char> (i + 1); current = this; }
    ~CandidateChainHost() { current = nullptr; }
    Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID iid, void** out) override
    {
        constexpr Steinberg::TUID reaperId = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
        *out = nullptr;
        if (std::memcmp (iid, reaperId, sizeof reaperId) != 0) return Steinberg::kNoInterface;
        *out = this; addRef(); return Steinberg::kResultOk;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++references; }
    Steinberg::uint32 PLUGIN_API release() override { return --references; }
    virtual void* PLUGIN_API getReaperApi (const char* name)
    {
        const juce::String key (name);
        if (key == "ValidatePtr2") return reinterpret_cast<void*> (+[] (void*, void*, const char*) { return true; });
        if (key == "TrackFX_GetCount") return reinterpret_cast<void*> (+[] (void*) { return current->postOnly ? 1 : (current->removed ? 4 : 5) - (current->sourceRemoved ? 1 : 0); });
        if (key == "TrackFX_GetRecCount") return reinterpret_cast<void*> (+[] (void*) { return 0; });
        if (key == "GetProjectStateChangeCount") return reinterpret_cast<void*> (+[] (void*) { return 1 + int (current->removed) + int (current->sourceRemoved) + int (current->hardwareA) + int (current->postOnly); });
        if (key == "GetPlayStateEx") return reinterpret_cast<void*> (+[] (void*) { return 1; });
        if (key == "Master_GetPlayRate") return reinterpret_cast<void*> (+[] (void*) { return 1.0; });
        if (key == "TrackFX_GetEnabled") return reinterpret_cast<void*> (+[] (void*, int slot) { return slot != 2 || !current->bypassed; });
        if (key == "TrackFX_GetOffline") return reinterpret_cast<void*> (+[] (void*, int) { return false; });
        if (key == "TrackFX_GetFXGUID") return reinterpret_cast<void*> (+[] (void*, int slot) -> const void*
        { return current->ids[static_cast<std::size_t> (current->postOnly ? 4 : slot)].data(); });
        if (key == "GetTrackGUID") return reinterpret_cast<void*> (+[] (void*) -> const void* { return current->ids[5].data(); });
        if (key == "GetMediaTrackInfo_Value") return reinterpret_cast<void*> (+[] (void*, const char* property)
        { return juce::String (property) == "I_NCHAN" ? 2.0 : 0.0; });
        if (key != "TrackFX_GetNamedConfigParm") return nullptr;
        return reinterpret_cast<void*> (+[] (void*, int slot, const char* property, char* out, int size)
        {
            if (current->unavailable) return false;
            if (current->postOnly) slot = 4;
            else if (current->sourceRemoved) ++slot;
            const juce::String field (property);
            juce::String value;
            const bool pre = slot == 0 || (slot == 2 && !current->removed);
            const bool post = current->postOnly || slot == (current->removed ? 3 : 4);
            if (field == "fx_type") value = "VST3";
            else if (field == "original_name") value = !pre && !post && (current->hardwareA ? slot == 1 : slot != 1) ? "ReaInsert" : "Effect";
            else if (field == "fx_ident") value = pre ? "pre.vst3{4B6972696E4879706861505245763031"
                : post ? "post.vst3{4B6972696E4879706861504F53547631" : "effect.vst3";
            else if (field == "chain_pdc_actual") value = juce::String (current->fill >= 0 ? current->fill : current->delay);
            else if (field == "chain_pdc_reporting" || field == "pdc") value = juce::String (current->reported >= 0 ? current->reported : current->delay);
            else if (field == "chain_pdc_mode") value = "1";
            else if (field == "parallel" || field == "instance_oversample_shift" || field == "chain_oversample_shift") value = "0";
            else return false;
            value.copyToUTF8 (out, static_cast<std::size_t> (size)); return true;
        });
    }
    virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 selector)
    { return selector == 1 || selector == 3 ? this : nullptr; }
    virtual void* PLUGIN_API reaperExtended (Steinberg::uint32, void*, void*, void*) { return nullptr; }
private:
    Steinberg::uint32 references = 1;
};
}
