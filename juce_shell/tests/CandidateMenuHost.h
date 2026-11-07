#pragma once
#include <pluginterfaces/base/funknown.h>
#include <array>

namespace hypha::tests
{
// Read-only public host interface fixture. The saved chunks come from actual processors;
// no synthetic candidate identity is injected into the product's list or menu.
class CandidateMenuHost final : public Steinberg::FUnknown
{
public:
    inline static CandidateMenuHost* current = nullptr;
    std::array<juce::String, 5> chunks;
    bool available = true, duplicate = false;
    CandidateMenuHost (KirinHyphaProcessorBase& first, KirinHyphaProcessorBase& second)
    {
        juce::MemoryBlock data;
        first.getStateInformation (data); chunks[4] = juce::Base64::toBase64 (data.getData(), data.getSize());
        second.getStateInformation (data); chunks[1] = juce::Base64::toBase64 (data.getData(), data.getSize());
        current = this;
    }
    ~CandidateMenuHost() { current = nullptr; }
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
        if (key == "TrackFX_GetCount") return reinterpret_cast<void*> (+[] (void*) { return 5; });
        if (key == "GetProjectStateChangeCount") return reinterpret_cast<void*> (+[] (void*) { return 1; });
        if (key != "TrackFX_GetNamedConfigParm") return nullptr;
        return reinterpret_cast<void*> (+[] (void*, int slot, const char* property, char* out, int size)
        {
            if (!current || !current->available || (slot != 1 && slot != 4)) return false;
            const juce::String field (property);
            juce::String value;
            if (field == "fx_type") value = "VST3";
            else if (field == "fx_ident") value = "effect.vst3{4B6972696E4879706861505245763031";
            else if (field == "renamed_name") value = slot == 1 ? "First insert" : "Last insert";
            else if (field == "vst_chunk") value = current->chunks[static_cast<std::size_t> (current->duplicate ? 1 : slot)];
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

template<class Host, class Require> void verifyCandidateMenuHost (Host& host, Require require)
{
    CandidateMenuHost api (host.pre1(), host.pre2());
    auto& post = host.post();
    post.getVST3ClientExtensions()->setIHostApplication (&api);
    auto names = post.enumeratePreCandidatesForMenu();
    require (names.size() == 2 && names[0].instanceId == host.pre2().instanceId()
        && names[0].chainPosition == 1 && names[1].chainPosition == 4,
        "real processor menu enumerates actual host slot order, independently of UUID order");
    require (hypha::pair_labels::menuLabel (names, names[0]).contains ("First insert"),
             "host's insert label distinguishes the PRE position");
    api.duplicate = true; names = post.enumeratePreCandidatesForMenu();
    require (names[0].chainPosition == -1 && names[1].chainPosition == -1,
             "duplicate restored identities never guess a host position");
    api.duplicate = false; api.available = false; names = post.enumeratePreCandidatesForMenu();
    require (names[0].chainPosition == -1 && names[1].chainPosition == -1,
             "missing host evidence preserves identifiable PRE labels without inferred order");
    api.available = true;
    // Marked like the pair list, never refused: the listener arranges the chain.
    auto other = std::make_unique<KirinHyphaProcessorBase> (KirinHyphaProcessorBase::Role::Post);
    other->setPlayHead (&host.clocks[2]);
    prepareFixtureProcessor (*other, 48000, host.block);
    LiveTimingFixtureAccess::enableFixtureWrites (*other);
    require (other->setPairCandidate (host.pre2().instanceId(), "Source"), "another POST claims the PRE offered as PRE2");
    host.passengers.push_back (other.get());
    host.render (40, false, false);
    awaitAnalysisInput (*other); awaitAnalysisExchange (*other, true);
    candidateMenuExpectedText = "In use by another POST: ";
    enterCandidateThroughEditor (post, require);
    candidateMenuExpectedText = {};
    host.passengers.clear();
    post.getVST3ClientExtensions()->setIHostApplication (nullptr);
}
}
