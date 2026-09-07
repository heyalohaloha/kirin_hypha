#include "HostContext.h"
#include "PresonusContextInfoProvider3.h"

#include <pluginterfaces/vst/ivsthostapplication.h>
#include <juce_audio_processors/format_types/pslextensions/ipslcontextinfo.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>

namespace hypha::local_blind
{
namespace
{
using namespace Steinberg;
bool matches (const TUID a, const TUID b) noexcept { return std::memcmp (a, b, sizeof (TUID)) == 0; }

struct Activity
{
    std::atomic<std::uint64_t> value { 1 };
    std::atomic<std::uint64_t> componentHandlerSets { 0 };
    std::atomic<std::uint64_t> hostApplicationSets { 0 };
    std::atomic<std::uint64_t> editControllerQueries { 0 };
    std::atomic<std::uint64_t> handlerInterfaceQueries { 0 };
    std::atomic<std::uint64_t> notifications { 0 };
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
};

class Notification final : public Presonus::IContextInfoHandler, public Presonus::IContextInfoHandler2
{
public:
    explicit Notification (std::shared_ptr<Activity> v) : activity (std::move (v)) {}
    tresult PLUGIN_API queryInterface (const TUID iid, void** out) override
    {
        if (out == nullptr) return kInvalidArgument;
        *out = nullptr;
        if (matches (iid, Presonus::IContextInfoHandler_iid))
            *out = static_cast<Presonus::IContextInfoHandler*> (this);
        else if (matches (iid, Presonus::IContextInfoHandler2_iid) || matches (iid, FUnknown_iid))
            *out = static_cast<Presonus::IContextInfoHandler2*> (this);
        if (*out == nullptr) return kNoInterface;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return refs.fetch_add (1) + 1; }
    uint32 PLUGIN_API release() override
    {
        const auto remaining = refs.fetch_sub (1) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }
    void PLUGIN_API notifyContextInfoChange() override { revoke(); }
    void PLUGIN_API notifyContextInfoChange (FIDString) override { revoke(); }
private:
    void revoke() noexcept
    {
        activity->notifications.fetch_add (1, std::memory_order_relaxed);
        activity->value.fetch_add (1, std::memory_order_seq_cst);
    }
    std::atomic<uint32> refs { 1 };
    std::shared_ptr<Activity> activity;
};

template <class T> struct Retained
{
    T* ptr = nullptr;
    Retained() = default;
    explicit Retained (T* p) : ptr (p) { if (ptr != nullptr) ptr->addRef(); }
    Retained (Retained&& other) noexcept : ptr (std::exchange (other.ptr, nullptr)) {}
    Retained& operator= (Retained&& other) noexcept
    {
        std::swap (ptr, other.ptr);
        return *this;
    }
    ~Retained() { if (ptr != nullptr) ptr->release(); }
};

template <class T> Retained<T> query (FUnknown* object, const TUID iid)
{
    Retained<T> result;
    void* value = nullptr;
    if (object != nullptr && object->queryInterface (iid, &value) == kResultOk)
        result.ptr = static_cast<T*> (value); // queryInterface already retained it.
    return result;
}

template <std::size_t N> bool decode (const std::array<Vst::TChar, N>& buffer, std::u16string& out)
{
    const auto end = std::find (buffer.begin(), buffer.end(), Vst::TChar {});
    // A full buffer may be a silently truncated opaque ID. Never use that prefix as identity.
    if (end == buffer.end() || end == buffer.end() - 1 || end == buffer.begin()) return false;
    for (auto p = buffer.begin(); p != end; ++p)
    {
        const auto c = static_cast<std::uint16_t> (*p);
        if (c < 0x20 || c == 0x7f || c == 0xfffe || c == 0xffff) return false;
        if (c >= 0xd800 && c <= 0xdbff)
        {
            if (++p == end || *p < 0xdc00 || *p > 0xdfff) return false;
        }
        else if (c >= 0xdc00 && c <= 0xdfff) return false;
    }
    out.assign (buffer.begin(), end);
    return true;
}

HostContextIssue readString (Presonus::IContextInfoProvider& provider, FIDString id, std::u16string& out)
{
    std::array<Vst::TChar, 256> buffer;
    buffer.fill (static_cast<Vst::TChar> (0xffff));
    if (provider.getContextInfoString (buffer.data(), static_cast<int32> (buffer.size()), id) != kResultOk)
        return HostContextIssue::unavailable;
    return decode (buffer, out) ? HostContextIssue::none : HostContextIssue::malformed;
}
}

const char* hostContextReadStageName (HostContextReadStage stage) noexcept
{
    switch (stage)
    {
        case HostContextReadStage::none: return "none";
        case HostContextReadStage::contextProvider: return "context provider";
        case HostContextReadStage::hostApplication: return "host application";
        case HostContextReadStage::hostName: return "host name";
        case HostContextReadStage::document: return "document ID";
        case HostContextReadStage::activeDocument: return "active document ID";
        case HostContextReadStage::channel: return "channel ID";
        case HostContextReadStage::activeDocumentMatch: return "active document match";
        case HostContextReadStage::revision: return "read revision";
    }
    return "unknown";
}

const char* hostContextProviderApiName (HostContextProviderApi api) noexcept
{
    switch (api)
    {
        case HostContextProviderApi::none: return "none";
        case HostContextProviderApi::v1: return "v1";
        case HostContextProviderApi::v2: return "v2";
        case HostContextProviderApi::v3: return "v3";
    }
    return "unknown";
}

struct HostContext::Impl
{
    std::shared_ptr<Activity> activity = std::make_shared<Activity>();
    Notification* notification = new Notification (activity);
    mutable std::mutex mutex;
    Retained<Steinberg::FUnknown> component, application;
    ~Impl() { notification->release(); }
    void replace (Retained<Steinberg::FUnknown>& slot, Steinberg::FUnknown* value)
    {
        Retained<Steinberg::FUnknown> next (value);
        {
            const std::lock_guard<std::mutex> guard (mutex);
            slot = std::move (next);
            activity->value.fetch_add (1, std::memory_order_seq_cst);
        } // Release the previous interface outside our mutex (host callbacks may re-enter).
    }
};

HostContext::HostContext() : impl (std::make_unique<Impl>()) {}
HostContext::~HostContext() = default;
void HostContext::setComponentHandler (Steinberg::FUnknown* value)
{
    impl->activity->componentHandlerSets.fetch_add (1, std::memory_order_relaxed);
    impl->replace (impl->component, value);
}
void HostContext::setHostApplication (Steinberg::FUnknown* value)
{
    impl->activity->hostApplicationSets.fetch_add (1, std::memory_order_relaxed);
    impl->replace (impl->application, value);
}
std::int32_t HostContext::queryEditController (const Steinberg::TUID iid, void** out)
{
    impl->activity->editControllerQueries.fetch_add (1, std::memory_order_relaxed);
    if (matches (iid, Presonus::IContextInfoHandler_iid)
        || matches (iid, Presonus::IContextInfoHandler2_iid))
        impl->activity->handlerInterfaceQueries.fetch_add (1, std::memory_order_relaxed);
    return impl->notification->queryInterface (iid, out);
}
std::uint64_t HostContext::revisionRealtime() const noexcept
{
    return impl->activity->value.load (std::memory_order_seq_cst);
}

HostContextFacts HostContext::readNonRealtime() const
{
    HostContextFacts result;
    Retained<Steinberg::FUnknown> component, application;
    {
        const std::lock_guard<std::mutex> guard (impl->mutex);
        result.revision = revisionRealtime();
        component = Retained<Steinberg::FUnknown> (impl->component.ptr);
        application = Retained<Steinberg::FUnknown> (impl->application.ptr);
    }
    auto provider3 = query<ContextInfoProvider3> (component.ptr, ContextInfoProvider3_iid);
    auto provider2 = provider3.ptr == nullptr
        ? query<Presonus::IContextInfoProvider2> (component.ptr, Presonus::IContextInfoProvider2_iid)
        : Retained<Presonus::IContextInfoProvider2> {};
    auto provider1 = provider3.ptr == nullptr && provider2.ptr == nullptr
        ? query<Presonus::IContextInfoProvider> (component.ptr, Presonus::IContextInfoProvider_iid)
        : Retained<Presonus::IContextInfoProvider> {};
    Presonus::IContextInfoProvider* provider = nullptr;
    if (provider3.ptr != nullptr)
    {
        provider = provider3.ptr;
        result.providerApi = HostContextProviderApi::v3;
    }
    else if (provider2.ptr != nullptr)
    {
        provider = provider2.ptr;
        result.providerApi = HostContextProviderApi::v2;
    }
    else if (provider1.ptr != nullptr)
    {
        provider = provider1.ptr;
        result.providerApi = HostContextProviderApi::v1;
    }
    auto host = query<Steinberg::Vst::IHostApplication> (application.ptr, Steinberg::Vst::IHostApplication_iid);
    if (provider != nullptr) result.failedAt = HostContextReadStage::hostApplication;
    if (provider != nullptr && host.ptr != nullptr)
    {
        result.failedAt = HostContextReadStage::hostName;
        std::array<Steinberg::Vst::TChar, 128> name;
        name.fill (static_cast<Steinberg::Vst::TChar> (0xffff));
        if (host.ptr->getName (name.data()) == Steinberg::kResultOk)
        {
            result.issue = decode (name, result.host) ? HostContextIssue::none : HostContextIssue::malformed;
            struct Field { Steinberg::FIDString id; std::u16string& value; HostContextReadStage stage; };
            const Field fields[] {
                { Presonus::ContextInfo::kDocumentID, result.document, HostContextReadStage::document },
                { Presonus::ContextInfo::kActiveDocumentID, result.activeDocument, HostContextReadStage::activeDocument },
                { Presonus::ContextInfo::kID, result.channel, HostContextReadStage::channel }
            };
            for (const auto& field : fields)
                if (result.issue == HostContextIssue::none)
                {
                    result.failedAt = field.stage;
                    result.issue = readString (*provider, field.id, field.value);
                }
            if (result.issue == HostContextIssue::none && result.document != result.activeDocument)
            {
                result.failedAt = HostContextReadStage::activeDocumentMatch;
                result.issue = HostContextIssue::inactiveDocument;
            }
        }
    }
    if (result.revision != revisionRealtime())
    {
        result.failedAt = HostContextReadStage::revision;
        result.issue = HostContextIssue::changedDuringRead;
    }
    result.componentHandlerSets = impl->activity->componentHandlerSets.load (std::memory_order_relaxed);
    result.hostApplicationSets = impl->activity->hostApplicationSets.load (std::memory_order_relaxed);
    result.editControllerQueries = impl->activity->editControllerQueries.load (std::memory_order_relaxed);
    result.handlerInterfaceQueries = impl->activity->handlerInterfaceQueries.load (std::memory_order_relaxed);
    result.notifications = impl->activity->notifications.load (std::memory_order_relaxed);
    // Never leave a partially read identity available for accidental fallback.
    if (! result.hasActiveIdentity())
    {
        result.host.clear();
        result.document.clear();
        result.activeDocument.clear();
        result.channel.clear();
    }
    else result.failedAt = HostContextReadStage::none;
    return result;
}
}
