#pragma once
// Shared fixed-fixture helpers with an explicit macOS thread CPU backend.
#include <juce_cryptography/juce_cryptography.h>
#include "../src/AtomicFile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
namespace hypha::tests::freq_paced
{
using Clock=std::chrono::steady_clock;
inline double ms(Clock::duration value) { return std::chrono::duration<double,std::milli>(value).count(); }
inline void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
struct ThreadCpu
{
#if defined(__APPLE__)
    mach_port_t port=mach_thread_self();
    ~ThreadCpu() { if(port!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),port); }
    double seconds() const {
        thread_basic_info_data_t info{}; mach_msg_type_number_t count=THREAD_BASIC_INFO_COUNT;
        check(thread_info(port,THREAD_BASIC_INFO,reinterpret_cast<thread_info_t>(&info),&count)==KERN_SUCCESS,
              "cannot obtain message-thread CPU time");
        return info.user_time.seconds+info.system_time.seconds
            +(info.user_time.microseconds+info.system_time.microseconds)/1.0e6;
    }
#else
    double seconds() const { throw std::runtime_error("paired benchmark CPU backend is macOS-only"); }
#endif
    ThreadCpu()=default; ThreadCpu(const ThreadCpu&)=delete; ThreadCpu& operator=(const ThreadCpu&)=delete;
};
inline juce::var object() { return juce::var(new juce::DynamicObject); }
inline void set(juce::var& value,const char* key,juce::var field) { value.getDynamicObject()->setProperty(key,field); }
inline double percentile(std::vector<double> values,double p) {
    check(!values.empty(),"cannot report percentile of empty samples"); std::sort(values.begin(),values.end());
    return values[static_cast<size_t>(std::ceil(p*values.size()))-1];
}
inline juce::var distribution(const std::vector<double>& values) {
    auto out=object(); set(out,"count",static_cast<int>(values.size()));
    if(values.empty()) { set(out,"valid",false); return out; }
    set(out,"valid",true); set(out,"p50",percentile(values,.50)); set(out,"p95",percentile(values,.95));
    set(out,"min",*std::min_element(values.begin(),values.end())); set(out,"max",*std::max_element(values.begin(),values.end()));
    return out;
}
inline void saveJson(const juce::File& file,const juce::var& value) {
    check(!file.exists(),"benchmark refuses to replace an existing result");
    check(hypha::storage::writeText(file,juce::JSON::toString(value,false)),"benchmark JSON write failed");
}
}
