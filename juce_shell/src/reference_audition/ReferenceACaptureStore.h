#pragma once
#include <juce_core/juce_core.h>
#include <memory>

namespace hypha::reference_audition
{
struct ACaptureData;
// Serialized ownership is independent of asynchronous decoding and of an active attempt.
// This class is only used on control/worker threads.
class ACaptureStore
{
public:
    struct Value { std::uint64_t generation=0; juce::String encoded; std::shared_ptr<const ACaptureData> document; };
    std::uint64_t beginRestore(const juce::String& payload)
    {
        const juce::ScopedLock guard(lock);
        ++current.generation; pending=true;
        // Never retain an unbounded host payload. Verification still belongs to the worker.
        incoming=payload.getNumBytesAsUTF8()<=1024*1024 ? payload : juce::String();
        rejectedOversize=payload.getNumBytesAsUTF8()>1024*1024;
        return current.generation;
    }
    std::uint64_t beginAttempt()
    {
        const juce::ScopedLock guard(lock);
        ++current.generation; pending=false; incoming.clear(); return current.generation;
    }
    bool finishRestore(std::uint64_t token,std::shared_ptr<const ACaptureData> decoded)
    {
        const juce::ScopedLock guard(lock);
        if(token!=current.generation || !pending) return false;
        if(decoded) { current.document=std::move(decoded); current.encoded=incoming; }
        else if(incoming.isEmpty() && !rejectedOversize) { current.document.reset(); current.encoded.clear(); }
        pending=false; incoming.clear(); return true;
    }
    bool commit(std::uint64_t token,std::shared_ptr<const ACaptureData> document,juce::String encoded)
    {
        const juce::ScopedLock guard(lock);
        if(token!=current.generation || pending || !document || encoded.isEmpty() || encoded.getNumBytesAsUTF8()>1024*1024) return false;
        current.document=std::move(document); current.encoded=std::move(encoded); return true;
    }
    bool isCurrent(std::uint64_t token) const
    { const juce::ScopedLock guard(lock); return token==current.generation; }
    Value value() const
    { const juce::ScopedLock guard(lock); auto result=current; if(pending && !rejectedOversize) result.encoded=incoming; return result; }
private:
    mutable juce::CriticalSection lock;
    Value current;
    juce::String incoming;
    bool pending=false,rejectedOversize=false;
};
}
