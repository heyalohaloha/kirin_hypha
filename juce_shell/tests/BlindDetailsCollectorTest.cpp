#include "HyphaBlindDetailsCollector.h"
#include <iostream>
#include <stdexcept>
namespace bd=hypha::blind_details;
namespace
{
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
KirinReferenceLiveMetricFrame level(std::uint64_t end)
{
    KirinReferenceLiveMetricFrame f{}; f.version=1; f.struct_size=sizeof f;
    f.sample_rate=48000; f.channels=2; f.measurement_epoch=10; f.generation=20;
    f.observed_frames=end; f.own_crest_db=8; f.pre_crest_db=5; f.own_psr_db=12; f.pre_psr_db=10; f.valid_mask=15;
    return f;
}
KirinPerceptualView sharp(std::int64_t end)
{
    KirinPerceptualView f{}; f.status=KIRIN_SPECTRUM_ACTIVE; f.has_data=1;
    f.sample_rate=48000; f.channels=2; f.channel_mode=KIRIN_SPECTRUM_CHANNEL_LR;
    f.aperture_samples=4800; f.presentation_end_samples=end; f.state_epoch_samples=100;
    f.pre_sharpness=1; f.post_sharpness=1.5; f.delta_sharpness=.5;
    return f;
}
KirinSpectrumView spectrum(std::int64_t end)
{
    KirinSpectrumView f{}; f.status=KIRIN_SPECTRUM_ACTIVE; f.has_data=1; f.post_has_data=1;
    f.sample_rate=48000; f.channels=2; f.channel_mode=KIRIN_SPECTRUM_CHANNEL_LR;
    f.aperture_samples=4800; f.presentation_end_samples=end; f.min_hz=20; f.max_hz=20000;
    for(unsigned i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i) { f.pre_dbfs[i]=-20; f.post_dbfs[i]=-18; f.display_db[i]=2; }
    return f;
}
void levelCases()
{
    bd::Collector c; c.begin();
    const std::uint64_t base=48000*3600ull;
    c.level(level(base)); require(c.result().psr.observations==0,"initial cumulative endpoint is baseline, not hours of evidence");
    for(unsigned i=1;i<=29;++i) { c.level(level(base+i*4800)); c.level(level(base+i*4800)); }
    auto r=c.result(); require(!r.psr.sufficient && r.psr.observations==29 && r.psr.seconds==2.9,"29 unique apertures below three seconds");
    c.level(level(base+30*4800)); r=c.result();
    require(r.psr.sufficient && r.psr.seconds==3 && r.psr.median==2 && r.crest.median==3,"three seconds and POST-PRE medians");
    c.freeze(); c.level(level(base+40*4800)); require(c.result().psr.observations==30,"reveal freezes evidence");
    c.begin(); c.level(level(base)); c.level(level(base+480000));
    require(c.result().psr.seconds==.1,"poll gap cannot manufacture missing observations");
    auto invalid=level(base+484800); invalid.valid_mask=5; c.level(invalid);
    require(c.result().psr.observations==1 && c.result().crest.observations==2,"missing PSR independent from CREST");
    auto changed=level(base+489600); changed.generation++; c.level(changed);
    require(c.result().psr.observations==0,"generation boundary erases old measurements and seeds new baseline");
    changed.observed_frames+=4800; c.level(changed); require(c.result().psr.observations==1,"new generation accepted once");
    changed.measurement_epoch++; c.level(changed); require(c.result().crest.observations==0,"epoch boundary resets");
    c.clear(); require(!c.active() && c.result().psr.observations==0,"END clears memory facts");
}
void sharpCases()
{
    bd::Collector c; c.begin(); KirinPerceptualBatch b{}; b.latest=sharp(480000); c.perceptual(b);
    b.count=31;
    for(unsigned i=0;i<b.count;++i) b.frames[i]=sharp(480000+i*4800);
    b.latest=b.frames[30]; c.perceptual(b); c.perceptual(b);
    auto r=c.result(); require(r.sharp.sufficient && r.sharp.observations==30 && r.sharp.seconds==3 && r.sharp.median==.5,"batch baseline filtered and duplicate latest counted once");
    b.latest.state_epoch_samples++; c.perceptual(b); require(c.result().sharp.observations==0,"SHARP state epoch cannot mix");
    b.count=0; b.latest.presentation_end_samples+=4800; b.latest.pre_sharpness=std::numeric_limits<double>::quiet_NaN(); c.perceptual(b);
    require(c.result().sharp.observations==0,"SHARP needs both sides not just a finite delta");
    b.latest.pre_sharpness=1; b.latest.presentation_end_samples+=2400; c.perceptual(b);
    require(c.result().sharp.seconds==.05,"overlapping aperture coverage counted once");
    b.count=KIRIN_PERCEPTUAL_BATCH_CAPACITY+1; c.perceptual(b);
    require(c.result().sharp.observations==1,"oversized batch is rejected");
}
void frequencyCases()
{
    bd::Collector c; c.begin(); KirinSpectrumBatch b{}; b.latest=spectrum(960000); c.spectrum(b,7);
    for(unsigned i=1;i<=30;++i) { b.latest=spectrum(960000+i*4800); c.spectrum(b,7); c.spectrum(b,7); }
    auto r=c.result(); require(r.hasFrequency && r.frequency[0].median==2 && r.frequency[255].seconds==3,"retain original 256 FREQ bands");
    c.spectrum(b,8); require(!c.result().hasFrequency && c.result().frequency[0].observations==0,"FREQ continuity reset");
    b.latest.presentation_end_samples+=4800; b.latest.pre_dbfs[0]=std::numeric_limits<float>::quiet_NaN(); c.spectrum(b,8);
    require(c.result().frequency[0].observations==0 && c.result().frequency[1].observations==1,"band-specific validity and coverage");
    b.latest.min_hz=30; c.spectrum(b,8); require(c.result().frequency[1].observations==0,"frequency partition change resets all bands");
}
void boundedMedian()
{
    bd::Series s; for(unsigned i=0;i<30;++i) s.add(i%2?4:2,4800);
    require(s.summary(48000).median==3,"even-count median");
    s.add(std::numeric_limits<double>::infinity(),4800); require(s.summary(48000).observations==30,"reject infinity");
    for(std::size_t i=30;i<=bd::Series::capacity;++i) s.add(1,4800);
    require(s.summary(48000).capacityExceeded && !s.summary(48000).sufficient,"bounded full-trial collector never silently reports a truncated median");
}
}
int main()
{
    try { levelCases(); sharpCases(); frequencyCases(); boundedMedian(); std::cout<<"Blind details collector contracts passed\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
