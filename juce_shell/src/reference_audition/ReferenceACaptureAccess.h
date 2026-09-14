#pragma once
// Included after the document/state definitions in ReferenceACaptureModel.h.
namespace hypha::reference_audition
{
// One non-RT transaction owns admission through commit and resource retirement.
// Queue consumption acknowledges delivery, never completion of that transaction.
class ACaptureAccess
{
public:
    enum Command { none, start, finish, cancel };
    CaptureRequestResult requestDetailed(Command command,std::optional<std::uint64_t> expected={})
    {
        const juce::ScopedLock lock(mutex);
        if(!alive || operation.phase==CaptureOperationPhase::closed) return CaptureRequestResult::unavailable;
        if(command==start)
        {
            if(!operation.canStart() || active || blind!=CaptureBlindOwner::none) return CaptureRequestResult::inProgress;
            if(expected && *expected!=operation.id) return CaptureRequestResult::stale;
            operation={CaptureOperationPhase::starting,store.beginAttempt(),false,false};
            commandGeneration=operation.id; pending=start;
        }
        else
        {
            if(expected && *expected!=operation.id) return CaptureRequestResult::stale;
            if(!operation.busy() || operation.phase==CaptureOperationPhase::restoring || operation.committed)
                return CaptureRequestResult::unavailable;
            if(command==cancel) operation.cancellation=true;
            else if(command!=finish || (operation.phase!=CaptureOperationPhase::capturing && operation.phase!=CaptureOperationPhase::armed))
                return CaptureRequestResult::unavailable;
            if(pending!=cancel) pending=command;
            commandGeneration=operation.id;
        }
        wake.signal(); return CaptureRequestResult::accepted;
    }
    bool request(Command command,std::optional<std::uint64_t> expected={})
    { return requestDetailed(command,expected)==CaptureRequestResult::accepted; }
    Command takeCommand(std::uint64_t& generation)
    { const juce::ScopedLock lock(mutex); generation=commandGeneration; return Command(pending.exchange(none)); }
    CaptureOperationView operationView() const
    { const juce::ScopedLock lock(mutex); return operation; }
    bool busy() const { return operationView().busy(); }
    bool reserveBlind(CaptureBlindOwner owner)
    {
        const juce::ScopedLock lock(mutex);
        if(!alive || !operation.canStart() || blind!=CaptureBlindOwner::none || owner==CaptureBlindOwner::none) return false;
        blind=owner; return true;
    }
    void releaseBlind(CaptureBlindOwner owner) { const juce::ScopedLock lock(mutex); if(blind==owner) blind=CaptureBlindOwner::none; }

    bool currentAttempt(std::uint64_t token) const
    {
        const juce::ScopedLock lock(mutex);
        return alive && operation.id==token && operation.busy() && operation.phase!=CaptureOperationPhase::restoring
            && !operation.cancellation && store.isCurrent(token);
    }
    bool advance(std::uint64_t token,CaptureOperationPhase phase)
    {
        const juce::ScopedLock lock(mutex);
        if(!alive || operation.id!=token || operation.cancellation || operation.phase==CaptureOperationPhase::restoring
            || !operation.busy() || operation.phase==CaptureOperationPhase::closed || !store.isCurrent(token)) return false;
        operation.phase=phase; return true;
    }
    void complete(std::uint64_t token)
    {
        const juce::ScopedLock lock(mutex);
        if(operation.id==token && operation.phase!=CaptureOperationPhase::restoring && operation.phase!=CaptureOperationPhase::closed)
        { operation.phase=CaptureOperationPhase::idle; pending=none; }
    }
    bool commitAttempt(std::uint64_t token,std::shared_ptr<const ACaptureData> document,juce::String encoded)
    {
        const juce::ScopedLock lock(mutex);
        if(!alive || operation.id!=token || operation.cancellation || operation.committed
            || operation.phase!=CaptureOperationPhase::finalizing) return false;
        if(!store.commit(token,std::move(document),std::move(encoded))) return false;
        operation.committed=true; return true;
    }
    std::uint64_t beginRestore(const juce::String& payload,bool shown)
    {
        const juce::ScopedLock lock(mutex);
        if(!alive) return 0;
        const auto token=store.beginRestore(payload);
        operation={CaptureOperationPhase::restoring,token,false,false}; pending=none; capturedView=shown; return token;
    }
    bool finishRestore(std::uint64_t token,std::shared_ptr<const ACaptureData> decoded)
    {
        const juce::ScopedLock lock(mutex);
        if(!alive || operation.id!=token || operation.phase!=CaptureOperationPhase::restoring) return false;
        return store.finishRestore(token,std::move(decoded));
    }
    void completeRestore(std::uint64_t token)
    {
        const juce::ScopedLock lock(mutex);
        if(operation.id==token && operation.phase==CaptureOperationPhase::restoring) operation.phase=CaptureOperationPhase::idle;
    }
    void close()
    {
        const juce::ScopedLock lock(mutex);
        alive=false; inputActive=false; operation.phase=CaptureOperationPhase::closed; pending=none;
    }
    void presentIfCurrent(std::uint64_t token,bool shown)
    { const juce::ScopedLock lock(mutex); if(alive && store.isCurrent(token)) capturedView=shown; }
    void publish(ACaptureState value,std::uint64_t token)
    { const juce::ScopedLock lock(mutex); if(alive && store.isCurrent(token)) state=std::move(value); }
    ACaptureStore store;
    ACaptureState snapshot() const
    { const juce::ScopedLock lock(mutex); auto value=state; value.operation=operation; value.observationFresh=value.observationFresh && inputActive.load(std::memory_order_acquire); return value; }
    // Fixture/publication utility; product operation ownership is never inferred from state.phase.
    void publish(ACaptureState value) { const juce::ScopedLock lock(mutex); state=std::move(value); }
    juce::WaitableEvent wake;
    std::atomic<std::uint64_t> framesProcessed{0},currentTimingEpoch{0};
    std::atomic<int> pending{none};
    std::atomic<bool> capturedView{false},active{false},alive{true},analysisAvailable{false},inputActive{false};
private:
    mutable juce::CriticalSection mutex;
    ACaptureState state;
    CaptureOperationView operation;
    CaptureBlindOwner blind=CaptureBlindOwner::none;
    std::uint64_t commandGeneration=0;
};
}
