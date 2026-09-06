#include "HostContext.h"

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

struct Revision
{
    std::atomic<std::uint64_t> value { 1 };
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
};

class Notification final : public Presonus::IContextInfoHandler, public Presonus::IContextInfoHandler2
{
public:
    explicit Notification (std::shared_ptr<Revision> v) : revision (std::move (v)) {}
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
    void revoke() noexcept { revision->value.fetch_add (1, std::memory_order_seq_cst); }
    std::atomic<uint32> refs { 1 };
    std::shared_ptr<Revision> revision;
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

struct HostContext::Impl
{
    std::shared_ptr<Revision> revision = std::make_shared<Revision>();
    Notification* notification = new Notification (revision);
    mutable std::mutex mutex;
    Retained<Steinberg::FUnknown> component, application;
    ~Impl() { notification->release(); }
    void replace (Retained<Steinberg::FUnknown>& slot, Steinberg::FUnknown* value)
    {
        Retained<Steinberg::FUnknown> next (value);
        {
            const std::lock_guard<std::mutex> guard (mutex);
            slot = std::move (next);
            revision->value.fetch_add (1, std::memory_order_seq_cst);
        } // Release the previous interface outside our mutex (host callbacks may re-enter).
    }
};

HostContext::HostContext() : impl (std::make_unique<Impl>()) {}
HostContext::~HostContext() = default;
void HostContext::setComponentHandler (Steinberg::FUnknown* value) { impl->replace (impl->component, value); }
void HostContext::setHostApplication (Steinberg::FUnknown* value) { impl->replace (impl->application, value); }
std::int32_t HostContext::queryEditController (const Steinberg::TUID iid, void** out)
{
    return impl->notification->queryInterface (iid, out);
}
std::uint64_t HostContext::revisionRealtime() const noexcept
{
    return impl->revision->value.load (std::memory_order_seq_cst);
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
    auto provider = query<Presonus::IContextInfoProvider> (component.ptr, Presonus::IContextInfoProvider_iid);
    auto host = query<Steinberg::Vst::IHostApplication> (application.ptr, Steinberg::Vst::IHostApplication_iid);
    if (provider.ptr != nullptr && host.ptr != nullptr)
    {
        std::array<Steinberg::Vst::TChar, 128> name;
        name.fill (static_cast<Steinberg::Vst::TChar> (0xffff));
        if (host.ptr->getName (name.data()) == Steinberg::kResultOk)
        {
            result.issue = decode (name, result.host) ? HostContextIssue::none : HostContextIssue::malformed;
            const std::pair<Steinberg::FIDString, std::u16string*> fields[] {
                { Presonus::ContextInfo::kDocumentID, &result.document },
                { Presonus::ContextInfo::kActiveDocumentID, &result.activeDocument },
                { Presonus::ContextInfo::kID, &result.channel }
            };
            for (const auto& field : fields)
                if (result.issue == HostContextIssue::none)
                    result.issue = readString (*provider.ptr, field.first, *field.second);
            if (result.issue == HostContextIssue::none && result.document != result.activeDocument)
                result.issue = HostContextIssue::inactiveDocument;
        }
    }
    if (result.revision != revisionRealtime()) result.issue = HostContextIssue::changedDuringRead;
    // Never leave a partially read identity available for accidental fallback.
    if (! result.hasActiveIdentity())
    {
        result.host.clear();
        result.document.clear();
        result.activeDocument.clear();
        result.channel.clear();
    }
    return result;
}
}
