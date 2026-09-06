#include "../src/local_blind/HostContext.h"
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <juce_audio_processors/format_types/pslextensions/ipslcontextinfo.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

using namespace Steinberg;
using namespace hypha::local_blind;
#define REQUIRE(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x "\n"; std::abort(); } } while (false)

namespace
{
bool same (const TUID a, const TUID b) { return std::memcmp (a, b, 16) == 0; }
class FakeHost final : public Presonus::IContextInfoProvider, public Vst::IHostApplication
{
public:
    std::atomic<uint32> refs { 1 };
    bool providerAvailable = true, nameAvailable = true, fieldAvailable = true;
    bool unterminated = false, capacityPrefix = false, changeWhileReading = false;
    std::u16string document = u"document-α", active = document, channel = u"channel-1";
    Presonus::IContextInfoHandler* observer = nullptr;
    int reads = 0, integerReads = 0;
    FUnknown* unknown() { return static_cast<Presonus::IContextInfoProvider*> (this); }
    tresult PLUGIN_API queryInterface (const TUID iid, void** out) override
    {
        if (out == nullptr) return kInvalidArgument;
        *out = nullptr;
        if (same (iid, Presonus::IContextInfoProvider_iid) && providerAvailable)
            *out = static_cast<Presonus::IContextInfoProvider*> (this);
        else if (same (iid, Vst::IHostApplication_iid))
            *out = static_cast<Vst::IHostApplication*> (this);
        if (*out == nullptr) return kNoInterface;
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API getName (Vst::String128 out) override
    {
        if (! nameAvailable) return kResultFalse;
        const std::u16string name = u"Test Host";
        std::copy (name.begin(), name.end(), out); out[name.size()] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance (TUID, TUID, void**) override { std::abort(); }
    tresult PLUGIN_API getContextInfoValue (int32&, FIDString) override
    { ++integerReads; return kResultFalse; }
    tresult PLUGIN_API getContextInfoString (Vst::TChar* out, int32 capacity, FIDString field) override
    {
        ++reads;
        if (changeWhileReading && observer != nullptr) observer->notifyContextInfoChange();
        if (! fieldAvailable) return kResultFalse;
        if (unterminated) { std::fill (out, out + capacity, u'x'); return kResultOk; }
        if (capacityPrefix) { std::fill (out, out + capacity - 1, u'x'); out[capacity - 1] = 0; return kResultOk; }
        const auto* value = std::strcmp (field, Presonus::ContextInfo::kDocumentID) == 0 ? &document
            : std::strcmp (field, Presonus::ContextInfo::kActiveDocumentID) == 0 ? &active
            : std::strcmp (field, Presonus::ContextInfo::kID) == 0 ? &channel : nullptr;
        REQUIRE (value != nullptr); // No names, folders, faders, routing or other host properties.
        REQUIRE (value->size() < static_cast<std::size_t> (capacity));
        std::copy (value->begin(), value->end(), out); out[value->size()] = 0;
        return kResultOk;
    }
};

void requireAbsent (const HostContextFacts& value, HostContextIssue issue)
{
    REQUIRE (! value.hasActiveIdentity()); REQUIRE (value.issue == issue);
    REQUIRE (value.host.empty() && value.document.empty() && value.activeDocument.empty() && value.channel.empty());
}

void factsAndFailureCases()
{
    FakeHost host;
    {
        HostContext context;
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        context.setHostApplication (host.unknown());
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        context.setComponentHandler (host.unknown());
        REQUIRE (host.refs == 3);
        const auto facts = context.readNonRealtime();
        REQUIRE (facts.hasActiveIdentity() && facts.host == u"Test Host");
        REQUIRE (facts.document == host.document && facts.activeDocument == host.active && facts.channel == host.channel);
        REQUIRE (facts.revision == context.revisionRealtime() && host.refs == 3);
        host.active = u"another document";
        requireAbsent (context.readNonRealtime(), HostContextIssue::inactiveDocument);
        host.active = host.document;
        host.providerAvailable = false;
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        host.providerAvailable = true; host.nameAvailable = false;
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        host.nameAvailable = true; host.fieldAvailable = false;
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        host.fieldAvailable = true; host.unterminated = true;
        requireAbsent (context.readNonRealtime(), HostContextIssue::malformed);
        host.unterminated = false;
        host.capacityPrefix = true;
        requireAbsent (context.readNonRealtime(), HostContextIssue::malformed);
        host.capacityPrefix = false;
        for (const auto& bad : { std::u16string {}, std::u16string (1, 0xd800),
                                std::u16string (1, 0xdc00), std::u16string (1, 0xffff), std::u16string (u"a\nb") })
        {
            host.channel = bad;
            requireAbsent (context.readNonRealtime(), HostContextIssue::malformed);
        }
        host.channel = u"チャンネル-\U0001f3b5";
        REQUIRE (context.readNonRealtime().hasActiveIdentity());
        context.setComponentHandler (nullptr);
        REQUIRE (host.refs == 2);
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        context.setComponentHandler (host.unknown());
        REQUIRE (context.readNonRealtime().hasActiveIdentity());
        context.setHostApplication (nullptr);
        requireAbsent (context.readNonRealtime(), HostContextIssue::unavailable);
        REQUIRE (host.integerReads == 0);
    }
    REQUIRE (host.refs == 1);
}

void notificationsAndLifetime()
{
    FakeHost host;
    Presonus::IContextInfoHandler* notification = nullptr;
    Presonus::IContextInfoHandler2* notification2 = nullptr;
    {
        HostContext context;
        context.setHostApplication (host.unknown()); context.setComponentHandler (host.unknown());
        void* out = nullptr;
        REQUIRE (context.queryEditController (Presonus::IContextInfoHandler_iid, &out) == kResultOk);
        notification = static_cast<Presonus::IContextInfoHandler*> (out);
        REQUIRE (context.queryEditController (Presonus::IContextInfoHandler2_iid, &out) == kResultOk);
        notification2 = static_cast<Presonus::IContextInfoHandler2*> (out);
        REQUIRE (context.queryEditController (Presonus::IContextInfoHandler_iid, nullptr) == kInvalidArgument);
        REQUIRE (context.queryEditController (Presonus::IContextInfoProvider2_iid, &out) == kNoInterface && out == nullptr);
        void* one = nullptr; void* two = nullptr;
        REQUIRE (notification->queryInterface (FUnknown_iid, &one) == kResultOk);
        REQUIRE (notification2->queryInterface (FUnknown_iid, &two) == kResultOk && one == two);
        static_cast<FUnknown*> (one)->release(); static_cast<FUnknown*> (two)->release();
        host.observer = notification;
        const auto revision = context.revisionRealtime();
        const auto reads = host.reads;
        std::thread callback ([&] { for (int i = 0; i < 10000; ++i) notification2->notifyContextInfoChange (nullptr); });
        callback.join();
        REQUIRE (context.revisionRealtime() == revision + 10000 && host.reads == reads);
        host.changeWhileReading = true;
        requireAbsent (context.readNonRealtime(), HostContextIssue::changedDuringRead);
        host.changeWhileReading = false;
        REQUIRE (context.readNonRealtime().hasActiveIdentity());
    }
    REQUIRE (host.refs == 1);
    // Host retains its notification interfaces longer than the processor: no dangling callback.
    notification->notifyContextInfoChange(); notification2->notifyContextInfoChange ("documentID");
    REQUIRE (notification->release() == 1); REQUIRE (notification2->release() == 0);
}
}

int main()
{
    factsAndFailureCases(); notificationsAndLifetime();
    std::cout << "Host context: identity, missing/malformed/inactive/replaced host, revision and retained notification PASS\n";
}
