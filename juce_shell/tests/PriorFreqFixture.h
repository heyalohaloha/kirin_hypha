#pragma once
#include <juce_cryptography/juce_cryptography.h>
#include "../src/AtomicFile.h"
#include "SpectrumPerformanceFixture.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaLanguage.h"
#include <stdexcept>
namespace hypha::tests::prior_freq {
inline int run(const juce::File& output) {
    if(output.exists() || !output.createDirectory().wasOk()) throw std::runtime_error("fresh output required");
    const auto fixture=spectrumPerformanceFixture();
    juce::Array<juce::var> cases;
    for(auto lang:{i18n::Language::english,i18n::Language::japanese}) {
        const i18n::ScopedLanguage language(lang);
        for(int width:{300,600,900}) {
            SpectrumComponent view;
            view.setPresentationContext(presentation::forEditor(width,width*2/3));
            view.setSize(width,width*2/3);
            view.setAbsoluteObservation(false); view.setLowScope(false);
            view.setDisplaySettings(display_settings::Values{});
            view.setAverageChoice(SpectrumComponent::Average::live);
            view.setSignalActive(true); view.setSnapshot(fixture);
            juce::Image image(juce::Image::ARGB,width,width*2/3,true);
            juce::Graphics graphics(image); text_style::ShownTextLog text;
            view.paintEntireComponent(graphics,true);
            const auto name=juce::String(width)+(lang==i18n::Language::english?"-en":"-ja");
            auto stream=output.getChildFile(name+".png").createOutputStream();
            if(!stream || !juce::PNGImageFormat().writeImageToStream(image,*stream)) throw std::runtime_error("PNG write failed");
            juce::MemoryOutputStream rgba;
            for(int y=0;y<image.getHeight();++y) for(int x=0;x<image.getWidth();++x) rgba.writeInt((int)image.getPixelAt(x,y).getARGB());
            auto row=new juce::DynamicObject(); row->setProperty("name",name);row->setProperty("width",width);row->setProperty("height",width*2/3);
            row->setProperty("rgba_sha256",juce::SHA256(rgba.getData(),rgba.getDataSize()).toHexString());
            juce::Array<juce::var> labels;
            for(int i=0;i<text.texts().size();++i){auto label=new juce::DynamicObject();label->setProperty("text",text.texts()[i]);label->setProperty("bounds",text.areas()[(size_t)i].toString());labels.add(juce::var(label));}
            row->setProperty("labels",labels); cases.add(juce::var(row));
        }
    }
    auto manifest=new juce::DynamicObject();manifest->setProperty("cases",cases);manifest->setProperty("backend","JUCE ARGB/CoreGraphics bitmap; paintEntireComponent; scale1");
    manifest->setProperty("mode","paired FULL LR AVG LIVE defaults; initial static snapshot; no hover");
    manifest->setProperty("manifest_writer","guarded-atomic-v1");
    manifest->setProperty("os",juce::SystemStats::getOperatingSystemName());manifest->setProperty("juce",juce::SystemStats::getJUCEVersion());
    if(!storage::writeText(output.getChildFile("render.json"), juce::JSON::toString(juce::var(manifest)))) throw std::runtime_error("manifest write failed");
    return 0;
}
}
