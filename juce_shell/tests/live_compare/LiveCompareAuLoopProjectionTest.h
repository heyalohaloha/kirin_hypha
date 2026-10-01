#pragma once
#include "LiveCompareLoopOracle.h"
#include <array>
#include <cstdio>
#include <cstdlib>

// Replays the clock convention observed in Studio Pro 8.1.2 on 2026-10-01:
// PRE VST3 -> 4096-sample delay -> POST AU, 48 kHz / 2048 frames, LOOP 0..8 PPQ.
// The PCM oracle is independently unique per emitted frame/lap, NOT the repeated WAV.
inline void verifyAuLoopProjection()
{
    using namespace loop_feasibility;
    const auto check=[](bool ok,const char* why) {
        if(!ok) {std::fprintf(stderr,"AU LOOP projection: %s\n",why);std::exit(1);}
    };
    for(int fault=0;fault<8;++fault)
    {
        auto ring=std::make_unique<Ring>(); ring->initialise(key,rate);
        Publisher publisher; Consumer consumer;
        std::array<std::array<float,2048>,2> input{},output{};
        const float* in[]={input[0].data(),input[1].data()};
        float* out[]={output[0].data(),output[1].data()};
        int accepted=0; std::int64_t knownK=0;
        for(int i=0;i<368;++i)
        {
            const std::int64_t emitted=static_cast<std::int64_t>(i)*2048;
            const auto project=emitted%192000;
            BlockClock pre{emitted,project,2048,true,true,true,false,{}};
            BlockClock post{11749376+emitted,project-4096,2048,true,true,true,false,{}};
            const auto truePostProject=post.project;
            if(post.project<0) ++post.project; // observed AU negative rounding, not a new K
            const bool loop=i>=59;
            pre.loop={loop,true,static_cast<double>(project)/24000.0,0,8,120};
            post.loop={loop,true,static_cast<double>(std::max<std::int64_t>(0,truePostProject))/24000.0,0,8,120};
            if(i==94) // first real wrap: PRE 512, AU -3583 / PPQ 0
            {
                switch(fault) {
                    case 1: post.project+=3; break;
                    case 2: post.project-=3; break;
                    case 3: post.project-=192000; break;
                    case 4: post.loop.ppq=0.25; break;
                    case 5: post.clock+=192000; break;
                    case 6: pre.afterGap=true; break;
                    case 7: post.loop.valid=false; break;
                    default: break;
                }
            }
            for(int c=0;c<2;++c) for(int j=0;j<2048;++j)
                input[static_cast<size_t>(c)][static_cast<size_t>(j)]=token(emitted+j,c);
            publisher.publish(*ring,pre,in,2);
            const auto d=consumer.process(*ring,key,rate,post,out,2);
            if(i==10) knownK=d.k;
            if(fault==0 && i>=10)
                check(d.verdict==Verdict::accepted && d.k==knownK && !d.timelineChanged,
                      "all frames after calibration, including AU clamp, preserve K and remain audible");
            if(fault!=0 && i==94)
                check(d.verdict!=Verdict::accepted,"contradictory native evidence cannot be canonicalized");
            if(d.verdict==Verdict::accepted) {
                ++accepted;
                for(int c=0;c<2;++c) for(int j=0;j<2048;++j)
                    check(out[c][j]==token(emitted-4096+j,c),"wrong native sample or lap reached output");
            }
        }
        if(fault==0) check(accepted==358,"observed 368-block run has only its 10 calibration blocks withheld");
    }
    std::puts("AU LOOP projection: observed clock sequence, per-lap PCM, 7 contradictory cases PASS");
}
