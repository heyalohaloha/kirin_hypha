#pragma once
#include "AaxChunkChain.h"
#include "../../src/live_compare/LiveComparePreAlign.h"

namespace live_compare_takeover_block_pcm_test
{
using namespace hypha::live_compare;
inline PreAlignState* interruptedOwner = nullptr;
inline const aax_chunk_test::Chain* interruptedChain = nullptr;
inline bool hookRan = false, blindMode = false;
inline int activeCallback=-1, hookCallback=-1;
inline void ownerTickInsideObserve (const AlignReadiness& clock)
{
    afterAlignWatchReadForTest = nullptr; hookRan = true; hookCallback=activeCallback;
    const auto& alignment = interruptedChain->postRenderer().alignState();
    PreAlignInput in; in.context = alignOwner ({}, interruptedChain->sharedRing());
    in.blind=blindMode; in.host.candidate=true; in.clock = &clock; in.revision = clock.generation(); in.k = clock.clockK();
    in.pending = clock.pending(); in.spent = alignment.budgetSpent();
    in.requested = alignment.requestFor (in.context); in.releasedAt = alignment.releasedAt();
    in.reach = alignment.servable(); in.agreement = 2;
    require (!interruptedOwner->transition (in).request, "owner cannot publish while real Audio takeover observation is incomplete");
}
inline void verifyCase(bool blind, bool selected, int frames, int move, bool unanswered)
{
        aax_chunk_test::Options options; options.k=-512; options.candidate=true; options.postFrames=frames;
        aax_chunk_test::Chain chain(options); chain.trial(blind); chain.select(selected); chain.run(30);
        const auto& alignment=chain.postRenderer().alignState();
        PreAlignState owner;
        const auto keepMeasured=[&]{
            PreAlignInput seed;seed.context=alignOwner({},chain.sharedRing());seed.clock=&alignment.readiness;
            seed.host.candidate=true;seed.blind=blind;seed.event=PreAlignEvent::estimate;seed.k=seed.measuredK=alignment.readiness.clockK();
            seed.revision=seed.measuredRevision=alignment.readiness.generation();seed.value={1000};seed.agreement=2;
            require(owner.transition(seed).request,"owner watches a measured correction before the clock changes");
        };
        if(unanswered){
            require(!alignment.budgetSpent(),"new case prepares its correction before the deadline");
            chain.confirmAlign({1000});chain.run(12);
            require(!alignment.budgetSpent() && alignment.value().frames==1000 && !alignment.readiness.pending(),
                    "real callbacks applied the retained correction before rechecking");
            keepMeasured();
            // Single-thread fixture preparation, before the observed window. No answer is published
            // after this new revision; expiration, K change and the race all use real callbacks.
            auto& prepared=const_cast<RendererAlignment&>(alignment);
            prepared.readiness.recheck();
            require(alignment.readiness.pending(),"recheck starts a genuinely unanswered revision");
        }
        for(int i=0;i<600&&!alignment.budgetSpent();++i){alignment.arm();chain.chunk();}
        require(alignment.budgetSpent(),"real callback fixture reaches the deadline");
        if(unanswered){
            require(owner.keptValue().frames==1000 && alignment.readiness.pending()
                    && alignment.requestFor(alignOwner({},chain.sharedRing())).frames==0,
                    "deadline retains the correction without a current published answer");
        }else{
            chain.relocate(48000);chain.run(4);chain.confirmAlign({96000});chain.run(12);
            keepMeasured();chain.confirmAlign({1000});
        }
        chain.setK(options.k+move);
        activeCallback=hookCallback=-1;blindMode=blind;interruptedOwner=&owner;interruptedChain=&chain;hookRan=false;afterAlignWatchReadForTest=ownerTickInsideObserve;
        const float carriedLast=chain.outputSample(frames-1);
        require(unanswered || !selected || chain.report.preAudible,"selected PRE is already audible before the clock change");
        int checked=0, callback=0, firstCallback=-1;float expectedWeight=0;bool havePrevious=false;
        const auto inspect=[&]{
            if(!havePrevious)return;
            const int currentCallback=callback++;
            if(unanswered) require(alignment.requestFor(alignOwner({},chain.sharedRing())).frames==0,
                                   "no response is published during either changed-K callback");
            if(checked==1) require(currentCallback==firstCallback+1 && chain.decision.verdict==Verdict::accepted && chain.decision.kValid && chain.decision.k==options.k+move, "the immediate next callback remains accepted at changed K");
            if(chain.decision.verdict!=Verdict::accepted||!chain.decision.kValid||chain.decision.k!=options.k+move){
                for(int f=0;f<frames;++f){
                    const int elapsed=currentCallback*frames+f;
                    const float position=std::min(1.0f,static_cast<float>(elapsed)/240.0f);
                    const float expected=selected?carriedLast*(1.0f-position)+aax_chunk_test::postSample*position:aax_chunk_test::postSample;
                    require(std::abs(chain.outputSample(f)-expected)<0.000002f,"raw changed K block and next preserve the existing withdrawal fade from the last emitted sample");
                }
                return;
            }
            require(chain.decision.preStart==chain.renderedClock-(options.k+move),"changed K callback reads the external expected PRE address");
            require(!chain.report.alignWaiting,"changed K callback adds no POST wait");
            for(int f=0;f<frames;++f){
                expectedWeight=selected?std::min(1.0f,expectedWeight+1.0f/240.0f):0.0f;
                const float expected=aax_chunk_test::postSample*(1.0f-expectedWeight)
                    +aax_chunk_test::pcm(chain.renderedClock-(options.k+move)+f,0)*expectedWeight;
                require(std::abs(chain.outputSample(f)-expected)<0.000002f,"real PCM uses uninterrupted per-sample PRE fade at new K");
            }
            if(checked==0)firstCallback=currentCallback;
            ++checked;
        };
        for(int chunk=0;chunk<16&&checked<2;++chunk){
            chain.chunk([&](int,BlockClock&){inspect();activeCallback=callback;havePrevious=true;});inspect();havePrevious=false;
        }
        std::printf("clock PCM unanswered=%d blind=%d selected=%d frames=%d move=%d hook=%d checked=%d\n",unanswered,blind,selected,frames,move,hookRan,checked);
        afterAlignWatchReadForTest=nullptr;interruptedOwner=nullptr;interruptedChain=nullptr;
        require(hookRan&&hookCallback==firstCallback&&checked>=2,"first changed K callback and its immediate successor were both inspected");
 }
inline void verify()
{
    for(bool blind:{false,true}) for(bool selected:{false,true})
        for(int frames:{64,128}) for(int move:{64,1000}) verifyCase(blind,selected,frames,move,false);
    verifyCase(false,true,128,64,true);
}
}
