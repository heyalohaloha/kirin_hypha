#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Steinberg { class FUnknown; using TUID = char[16]; }

namespace hypha::local_blind
{
enum class HostContextIssue
{
    none, unavailable, malformed, inactiveDocument, changedDuringRead
};

// Where observation failed, not permission to use partially obtained identity.
enum class HostContextReadStage
{
    none, contextProvider, hostApplication, hostName, document, activeDocument, channel,
    activeDocumentMatch, revision
};
const char* hostContextReadStageName (HostContextReadStage) noexcept;

// These are host observations, NOT an admission token. In particular they do not prove
// routing, PDC, all participants, or compatibility with an older plug-in in the document.
struct HostContextFacts
{
    std::u16string host, document, activeDocument, channel;
    std::uint64_t revision = 0;
    HostContextIssue issue = HostContextIssue::unavailable;
    HostContextReadStage failedAt = HostContextReadStage::contextProvider;
    bool hasActiveIdentity() const noexcept { return issue == HostContextIssue::none; }
};

// No persisted Hypha/Work identity is accepted here. Only the host's native VST3 extension
// can supply these facts. Reads belong to the host controller/main thread, never a worker
// or audio thread. The JUCE entry point enforces the message-thread boundary.
class HostContext
{
public:
    HostContext();
    ~HostContext();
    HostContext (const HostContext&) = delete;
    HostContext& operator= (const HostContext&) = delete;

    void setComponentHandler (Steinberg::FUnknown*);
    void setHostApplication (Steinberg::FUnknown*);
    std::int32_t queryEditController (const Steinberg::TUID, void**);
    HostContextFacts readNonRealtime() const;
    // Notifications only revoke the prior revision. They never query the host or publish
    // new authority. Safe even if a host retains its notification interface after destruction.
    std::uint64_t revisionRealtime() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
