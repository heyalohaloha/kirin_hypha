#pragma once
#include "kirin_hypha_reference_analysis_ffi.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
namespace hypha::reference_audition
{
// One engine owns one physical analysis slot. Every asynchronous job retains a
// grant until its last read/convert has returned, including after UI cancellation.
class ReferenceAnalysis final
{
    struct Owner
    {
        explicit Owner(KirinReferenceAnalysisOwner* p): value(p) {}
        ~Owner() { kirin_reference_analysis_owner_drop(value); }
        KirinReferenceAnalysisOwner* value;
    };
public:
    struct Grant
    {
        ~Grant() { kirin_reference_analysis_grant_drop(value); }
        std::uint64_t revision=0;
    private:
        friend class ReferenceAnalysis;
        KirinReferenceAnalysisGrant* value=nullptr;
        std::shared_ptr<Owner> owner;
    };
    using Lease=std::shared_ptr<const Grant>;
    ReferenceAnalysis(): owner(std::make_shared<Owner>(kirin_reference_analysis_create())) {}
    void replace(KirinReferenceAnalysisOwner* value)
    {
        auto next=std::make_shared<Owner>(value);
        const juce::ScopedLock lock(mutex);
        if(kirin_reference_analysis_same(owner->value,value)) return;
        retired.erase(std::remove_if(retired.begin(),retired.end(),[](const auto& p){return p.expired();}),retired.end());
        if(owner.use_count()>1) retired.push_back(owner); owner=std::move(next); ++revision;
    }
    bool current(const Lease& lease) const noexcept { return lease && lease->revision==revision.load(std::memory_order_acquire); }
    Lease acquire()
    {
        const juce::ScopedLock lock(mutex);
        retired.erase(std::remove_if(retired.begin(),retired.end(),[](const auto& p){return p.expired();}),retired.end());
        if(!retired.empty()) return {}; // Old engine jobs retire before the replacement admits work.
        auto* raw=kirin_reference_analysis_acquire(owner->value); if(!raw) return {};
        auto result=std::make_shared<Grant>(); result->value=raw; result->owner=owner; result->revision=revision; return result;
    }
private:
    mutable juce::CriticalSection mutex;
    std::shared_ptr<Owner> owner;
    std::vector<std::weak_ptr<Owner>> retired;
    std::atomic<std::uint64_t> revision{1};
};
}
