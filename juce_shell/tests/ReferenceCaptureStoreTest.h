#pragma once
#include "../src/reference_audition/ReferenceACaptureStore.h"
inline void testCaptureStore(const std::shared_ptr<const ref::ACaptureData>& good,const juce::String& encoded)
{
    ref::ACaptureStore store;
    auto token=store.beginRestore(encoded);
    require(store.value().encoded==encoded && !store.value().document,"restore is immediately serializable before decoding");
    const auto newer=store.beginRestore(encoded+"x");
    require(!store.finishRestore(token,good),"late restore cannot replace a newer request");
    require(store.finishRestore(newer,nullptr) && !store.value().document,"invalid first restore cannot become valid");
    token=store.beginAttempt(); require(store.commit(token,good,encoded),"capture atomically commits document and payload");
    token=store.beginRestore(encoded+"x");
    require(store.finishRestore(token,nullptr) && store.value().document==good && store.value().encoded==encoded,"bad restore retains last valid capture");
    token=store.beginRestore(encoded); const auto capture=store.beginAttempt();
    require(!store.finishRestore(token,good),"an older decode cannot overwrite a new capture intent");
    token=store.beginRestore(encoded);
    require(!store.commit(capture,good,encoded),"an older capture result cannot overwrite restored state");
    require(store.finishRestore(token,good),"latest restore completes");
    token=store.beginRestore({}); require(store.value().encoded.isEmpty(),"explicit empty restoration is immediately saveable");
    require(store.finishRestore(token,nullptr) && !store.value().document,"explicit empty restoration clears old capture");
}
