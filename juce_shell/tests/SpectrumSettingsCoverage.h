#pragma once
#include "SpectrumSettingsTuples.h"
#include <set>
#include <juce_cryptography/juce_cryptography.h>
namespace hypha::tests::settings_qa {
inline std::array<int,9> valuesOf(int id) {
    const auto tuple=decode(id); std::array<int,9> v{};
    for(size_t i=0;i<8;++i)v[i]=tuple.values.selected[i];
    v[8]=static_cast<int>(tuple.average);return v;
}
// Independent audit: encode explicit axis/value pairs, not generator stride arithmetic.
inline int pairCoverage(const std::vector<int>& ids) {
    std::set<std::array<int,4>> seen;
    for(int id:ids){const auto v=valuesOf(id);
        for(int a=0;a<9;++a)for(int b=a+1;b<9;++b)seen.insert({a,v[(size_t)a],b,v[(size_t)b]});}
    int expected=0,covered=0;
    for(int a=0;a<9;++a)for(int b=a+1;b<9;++b)
        for(int x=0;x<radices[(size_t)a];++x)for(int y=0;y<radices[(size_t)b];++y){
            ++expected;covered+=seen.count({a,x,b,y})?1:0;}
    require(expected==597,"pair denominator changed");return covered;
}
inline std::vector<int> pairwiseIds() {
    // Deterministic modest set: default, each single, and two nondefault axes.
    // No exhaustive Cartesian search or random generator. Not a minimum covering array.
    const auto singles=singleChoiceIds();std::set<int> selected(singles.begin(),singles.end());
    std::array<int,9> stride{};int step=1;
    for(size_t a=0;a<9;++a){stride[a]=step;step*=radices[a];}
    for(size_t a=0;a<9;++a)for(size_t b=a+1;b<9;++b)
        for(int x=1;x<radices[a];++x)for(int y=1;y<radices[b];++y)
            selected.insert(x*stride[a]+y*stride[b]);
    std::vector<int> result(selected.begin(),selected.end());
    require(pairCoverage(result)==597,"generated set does not cover all pairs");
    require(pairCoverage({})==0 && pairCoverage(singles)<597,"coverage audit accepts incomplete control");
    auto missing=result;missing.pop_back();
    require(pairCoverage(missing)<597,"coverage audit missed an omitted pair witness");
    return result;
}
inline std::vector<int> listIds(const juce::File& file) {
    require(file.existsAsFile()&&file.getSize()>0&&file.getSize()<=1024*1024,"invalid tuple list file size");
    const auto parsed=juce::JSON::parse(file);
    require(parsed.isObject()&&parsed.getProperty("schema",{})==juce::var("spectrum-settings-list-v1"),"bad tuple list schema");
    const auto list=parsed.getProperty("ids",{});const auto* array=list.getArray();
    require(array&&array->size()>0&&array->size()<=population,"tuple list is empty/oversized");
    std::vector<int> ids;std::set<int> unique;
    for(const auto& value:*array){
        require(value.isInt()||value.isInt64(),"tuple IDs must be integers");
        const auto id=static_cast<juce::int64>(value);
        require(id>=0&&id<population&&unique.insert(static_cast<int>(id)).second,"invalid/duplicate tuple ID");
        ids.push_back(static_cast<int>(id));
    }
    return ids;
}
inline juce::String idsHash(const std::vector<int>& ids) {
    juce::MemoryOutputStream bytes;bytes.writeString("settings-ordered-ids-v1");
    for(int id:ids)bytes.writeInt(id);
    return juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString();
}
}
