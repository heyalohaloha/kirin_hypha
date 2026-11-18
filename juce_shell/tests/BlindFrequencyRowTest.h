#pragma once
#include "../src/HyphaBlindScreen.h"
#include <stdexcept>

namespace hypha::tests
{
inline void verifyBlindFrequencyRow (const juce::String& preview)
{
    const auto check=[](bool ok,const char* why){if(!ok) throw std::runtime_error(why);};
    blind_details::Collector collector; collector.begin();
    KirinSpectrumBatch batch{};
    auto& frame=batch.latest;
    frame.status=KIRIN_SPECTRUM_ACTIVE; frame.has_data=frame.post_has_data=1;
    frame.sample_rate=48000; frame.channels=2; frame.channel_mode=KIRIN_SPECTRUM_CHANNEL_LR;
    frame.aperture_samples=4800; frame.presentation_end_samples=48000; frame.min_hz=20; frame.max_hz=20000;
    for(size_t i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i)
    { frame.pre_dbfs[i]=-60; frame.post_dbfs[i]=-50; frame.display_db[i]=2; }
    collector.spectrum(batch,7);
    for(int n=0;n<29;++n){ for(auto& value:frame.display_db) value=n<20?2.0f:8.0f; frame.presentation_end_samples+=4800;collector.spectrum(batch,7);}
    auto shortData=std::make_shared<const blind_details::Result>(collector.result());
    blind_ui::FrequencyRow row; row.setResult(shortData);
    check(!row.available(),"under three seconds has no frequency line");
    check(row.pointerText(.5f)==i18n::tr("Not shown because the measured time is too short"),"short evidence message");
    for(auto& value:frame.display_db) value=14;
    frame.presentation_end_samples+=4800; collector.spectrum(batch,7); collector.freeze();
    auto result=std::make_shared<blind_details::Result>(collector.result());
    check(result->frequency[0].median==2 && result->frequency[255].seconds==3,"display delta median, not raw difference");
    blind_details::Series even;
    for(int n=0;n<30;++n) even.add(n<15?1.0:5.0,4800);
    check(even.summary(48000).median==3,"even centre observations are averaged");
    result->frequency[128]={};
    row.setResult(result); check(row.available() && !row.valid(128) && row.valid(127),"missing bin is not drawn");
    check(row.pointerText(spectrum_geometry::bandCentreNormalisedX(127)).contains("+2.00 dB"),"exact valid bin pointer");
    check(row.pointerText(spectrum_geometry::bandCentreNormalisedX(128)).contains(i18n::tr("Not shown because the measured time is too short")),"missing bin pointer");
    check(!row.pointerText(.5f).contains("+2.00 dB"),"pointer must not bridge missing bin");
    int images=0;
    for(const auto language:{i18n::Language::english,i18n::Language::japanese})
    for(const int width:{300,600,900})
    {
        const i18n::ScopedLanguage scoped(language);
        blind_ui::ScreenComponent screen("frequency-test"); screen.setSize(width,width*2/3);
        blind_ui::Screen state; state.title="BLIND RESULT"; state.detailsShown=state.detailsOpen=true;
        state.detailsText="PSR: +1.00 dB\nCREST: +2.00 dB\nSHARP: +0.20 acum\n"+juce::String::repeatedString("Synthetic measurement\n",12);
        state.frequencyDetails=result; screen.setScreen(state);
        auto* viewport=dynamic_cast<juce::Viewport*>(screen.findChildWithID("frequency-test-details-viewport"));
        check(viewport && viewport->isVisible(),"frequency details uses visible scroll viewport");
        auto* graph=dynamic_cast<blind_ui::FrequencyRow*>(viewport->getViewedComponent()->findChildWithID("frequency-test-frequency-details"));
        check(graph && graph->isVisible() && graph->available(),"frequency graph connected to frozen screen");
        viewport->setViewPosition(0,viewport->getViewedComponent()->getHeight());
        for(const bool missing:{false,true})
        {
            state.frequencyDetails=missing?shortData:result; screen.setScreen(state);
            viewport->setViewPosition(0,viewport->getViewedComponent()->getHeight());
            const auto graphInContent=graph->getBounds();
            const auto visible=viewport->getViewArea();
            check(visible.contains(graphInContent),"whole frequency heading, plot and readout reachable together");
            check(graph->plotBounds().getHeight()>=38,"compact frequency plot has readable independent space");
            check(graph->textHeight()>=14.0f && graph->rowHeight()>=graph->textHeight()+4.0f,
                "semantic details font stays at least fourteen pixels with vertical room");
            check(!graph->headingBounds().intersects(graph->readoutBounds())
                && graph->readoutBounds().getBottom()<=graph->plotBounds().getY()-graph->rowHeight()*.5f,"heading and pointer do not overlap plot");
            juce::Rectangle<float> previousAxis;
            for(const float hz:{100.0f,1000.0f,10000.0f}) {
                const auto axis=graph->frequencyLabelBounds(hz);
                check(graph->getLocalBounds().toFloat().contains(axis) && !axis.intersects(previousAxis),
                    "semantic frequency labels fit and do not overlap");
                check(axis.getY()>=graph->plotBounds().getBottom(),"frequency labels stay below plot");
                previousAxis=axis;
            }
            const auto plot=graph->plotBounds();
            const auto pointer=juce::Point<float>(juce::jmap(spectrum_geometry::bandCentreNormalisedX(127),plot.getX(),plot.getRight()),plot.getCentreY());
            const auto now=juce::Time::getCurrentTime();
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),pointer,juce::ModifierKeys{},
                0,0,0,0,0,graph,graph,now,pointer,now,0,false);
            graph->mouseMove(event);
            text_style::ShownTextLog painted;
            auto image=screen.createComponentSnapshot(screen.getLocalBounds());
            const auto words=painted.texts().joinIntoString(" ");
            check(words.contains(i18n::tr("Frequency difference (POST - PRE)")),"frequency heading painted");
            check(missing ? words.contains(i18n::tr("Not shown because the measured time is too short"))
                          : words.contains("Hz:") && words.contains("+2.00 dB"),"mouseMove pointer readout painted");
            graph->mouseExit(event);
            text_style::ShownTextLog exited;
            screen.createComponentSnapshot(screen.getLocalBounds());
            check(!exited.texts().joinIntoString(" ").contains("Hz:"),"mouse exit clears pointer readout");
            check(image.isValid(),"frequency detail image");
            if(preview.isNotEmpty())
            {
                const auto file=juce::File(preview).getChildFile("frequency-"+juce::String(language==i18n::Language::japanese?"ja-":"en-")+juce::String(width)+(missing?"-short.png":"-measured.png"));
                check(!file.exists(),"frequency preview must be fresh"); auto out=file.createOutputStream();
                check(out && juce::PNGImageFormat().writeImageToStream(image,*out),"frequency preview write");
            }
            ++images;
        }
        state.frequencyDetails.reset(); state.detailsOpen=false; screen.setScreen(state);
        check(!viewport->isVisible(),"closed details hides frequency row");
    }
    check(images==12,"frequency image case count");
    std::cout<<"BLIND_FREQUENCY_CASES=12\n";
}
}
