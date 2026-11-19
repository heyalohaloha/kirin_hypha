#pragma once
#include "LiveCompareReleaseResendTest.h"

namespace live_compare_retry_handoff_test
{
using namespace hypha::live_compare;
static PreAlignState owner;
static RendererAlignment renderer;
static AlignContext context {3,5,7,9};
inline void retiresUncheckedMarker() {
 renderer.readiness.observe(true,true,true,0);
 PreAlignInput seed; seed.context=context;seed.clock=&renderer.readiness;seed.event=PreAlignEvent::estimate;
 seed.revision=seed.measuredRevision=renderer.readiness.generation();seed.value={1000};seed.agreement=2;
 const auto out=owner.transition(seed);renderer.request(out.value,context,out.revision);
 afterAlignWatchReadForTest=[](const AlignReadiness& clock){
  PreAlignInput tick;tick.context=context;tick.clock=&clock;tick.revision=clock.generation();tick.k=clock.clockK();
  tick.pending=tick.spent=true;tick.reach=48000;tick.agreement=2;
  owner.transition(tick);
 };
 renderer.readiness.observe(true,true,true,64);afterAlignWatchReadForTest=nullptr;
 const auto revision=renderer.readiness.generation();
 require(renderer.readiness.provisionalClockIs(revision), "unchecked tick really installs its marker");
 PreAlignInput checked;checked.context=context;checked.clock=&renderer.readiness;checked.revision=revision;
 checked.k=64;checked.pending=checked.spent=true;checked.agreement=2;
 checked.reach=std::numeric_limits<std::int64_t>::max();checked.now=100;checked.readRetryNanos=500;
 const auto retry=owner.transition(checked);
 require(!retry.request, "unknown reach chooses bounded reread");
 const bool stale=renderer.readiness.provisionalClockIs(revision);
 require(!stale, "checked reread retires provisional and hands off without a gap");
}
inline void holdsFallbackAcrossRequestPublication(){
 using namespace live_compare_release_resend_test;
 for(bool blind:{false,true})for(bool selected:{false,true}){
  ClockCase c(blind,selected,false);
  c.r.readiness.provisionalClockFor(c.r.readiness.generation());
  auto tick=c.input(PreAlignEvent::tick);tick.reach=std::numeric_limits<std::int64_t>::max();
  require(!c.send(tick).request,"unknown reach must choose bounded reread");
  auto update=c.r.update(c.playback,blind,selected,c.weight,48000,128,64);
  const auto response=c.tick();require(response.request,"ready owner publishes its answer after RT snapshot");
  c.r.resolve(c.k,c.weight);c.r.readiness.observe(true,true,true,c.k);c.r.observed(true,true,update);
  require(c.r.value().frames==0&&!c.r.awaits(update),"publish/clear after request snapshot must not add one POST block");
 }
 std::puts("retry publish snapshot race: PASS");
}
inline void twoEarlyOwnerTicksPreserveTheFallback(){
 using namespace live_compare_release_resend_test;
 for(bool blind:{false,true})for(bool selected:{false,true}){
  ClockCase c(blind,selected,false);
  auto update=c.r.update(c.playback,blind,selected,c.weight,48000,128,64);
  auto tick=c.input(PreAlignEvent::tick);tick.reach=std::numeric_limits<std::int64_t>::max();
  require(!c.send(tick).request,"unknown reach must choose bounded reread");
  const auto response=c.tick();require(response.request,"ready owner publishes its answer after RT snapshot");
  c.r.resolve(c.k,c.weight);c.r.readiness.observe(true,true,true,c.k);c.r.observed(true,true,update);
  require(c.r.value().frames==0&&!c.r.awaits(update),"two owner ticks after an unmarked snapshot must not add one POST block");
 }
 std::puts("retry two early ticks: PASS");
}
}
