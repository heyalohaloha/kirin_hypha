#include "HyphaReferenceComparisonView.h"
#include "HyphaTheme.h"
namespace hypha::reference_ui
{
void ComparisonView::rebuildCaptured()
{
    const auto& a=*data->capture; const int channels=a.channels;
    const int columns=juce::jlimit(1,900,int(std::ceil(waveform.getWidth()))),height=juce::jmax(1,int(std::ceil(waveform.getHeight())));
    const double gain=std::pow(10.0,data->binding.gainDb/20.0);
    std::array<std::vector<double>,4> radius; for(auto& v:radius) v.resize(size_t(columns));
    std::vector<bool> changed(size_t(columns),false); std::vector<bool> bKnown(size_t(columns),false);
    double scale=1;
    for(int x=0;x<columns;++x)
    {
        const double first=double(x)*double(a.frames)/columns,last=double(x+1)*double(a.frames)/columns;
        double ap=0,ae=0,bp=0,be=0,bFrames=0;
        auto it=std::upper_bound(a.bins.begin(),a.bins.end(),std::uint64_t(first),[](auto v,const auto& bin){return v<bin.offset;});
        size_t i=it==a.bins.begin() ? 0 : size_t(it-a.bins.begin()-1);
        for(;i<a.bins.size() && double(a.bins[i].offset)<last;++i)
        {
            const auto& original=a.bins[i]; const auto& pair=data->bins[i];
            const double frames=std::max(0.0,std::min(last,double(original.offset+original.value.frames))-std::max(first,double(original.offset)));
            ap=std::max({ap,pair.a.peak[0],pair.a.peak[1]});
            ae+=(pair.a.rms[0]*pair.a.rms[0]+pair.a.rms[1]*pair.a.rms[1])/channels*frames;
            if(pair.b.frames==pair.a.frames)
            { bp=std::max({bp,pair.b.peak[0],pair.b.peak[1]}); be+=(pair.b.rms[0]*pair.b.rms[0]+pair.b.rms[1]*pair.b.rms[1])/channels*frames; bFrames+=frames; }
            changed[size_t(x)]=changed[size_t(x)] || (i<data->revisited.size() && data->revisited[i]==2);
        }
        radius[0][size_t(x)]=ap; radius[1][size_t(x)]=std::sqrt(ae/(last-first));
        radius[2][size_t(x)]=bp*gain; radius[3][size_t(x)]=std::sqrt(be/(last-first))*gain;
        bKnown[size_t(x)]=bFrames>=last-first-1e-6; scale=std::max({scale,ap,bp*gain});
    }
    scale=std::pow(2.0,std::ceil(std::log2(scale)));
    waveformCache=juce::Image(juce::Image::ARGB,columns,height,true); juce::Graphics g(waveformCache);
    for(int layer=0;layer<4;++layer)
    {
        g.setColour((layer<2 ? COL_SPECTRUM_DELTA_BR : COL_FLORA).withAlpha(layer%2 ? 0.85f : 0.35f));
        const auto center=height*(layer<2 ? 0.25 : 0.75);
        for(int x=0;x<columns;++x) if(layer<2 || bKnown[size_t(x)])
        {
            const auto n=std::max(0.5,radius[size_t(layer)][size_t(x)]/scale*height*0.20);
            g.fillRect(x,int(std::floor(center-n)),1,juce::jmax(1,int(std::ceil(n*2))));
        }
    }
    // A thin neutral marker identifies revisited input changes, without grading them.
    g.setColour(COL_TEXT_SECONDARY.withAlpha(0.65f));
    for(int x=0;x<columns;++x) if(changed[size_t(x)]) g.fillRect(x,0,1,2);
    cacheRevision=data->revision;
}
juce::String ComparisonView::capturedValuesAt(double seconds,bool compact) const
{
    const auto& capture=*data->capture;
    if(seconds<0 || seconds>capture.duration()) seconds=std::min(end,capture.duration());
    const auto it=std::upper_bound(capture.bins.begin(),capture.bins.end(),std::uint64_t(seconds*capture.rate),
        [](auto v,const auto& b){return v<b.offset+b.value.frames;});
    if(it==capture.bins.begin()) return "A  --    B  --";
    const size_t index=size_t(it-capture.bins.begin()-1); const auto& pair=data->bins[index];
    const double a=showingCrest ? pair.a.crest_db : pair.a.short_lufs;
    const double b=pair.b.frames ? (showingCrest ? pair.b.crest_db : pair.b.short_lufs+data->binding.gainDb) : std::numeric_limits<double>::quiet_NaN();
    const auto number=[](double v){return std::isfinite(v) ? juce::String(v,1) : juce::String("--");};
    auto result=juce::String("A ")+number(a)+"   B "+number(b)+(showingCrest ? " dB" : " LUFS-S");
    if(!compact && std::isfinite(a) && std::isfinite(b)) result+="   B-A "+number(b-a)+(showingCrest ? " dB" : " LU");
    if(index<data->revisited.size() && data->revisited[index]==2) result+=" / A CHANGED";
    if(!compact && capture.complete)
        result+="   I "+number(capture.integrated)+" LUFS / TP "+number(capture.maximumTruePeak>0 ? 20*std::log10(capture.maximumTruePeak) : -INFINITY)+" dBTP";
    return result;
}
}
