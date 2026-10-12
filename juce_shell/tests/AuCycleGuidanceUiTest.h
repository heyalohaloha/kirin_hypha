#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaPerceptualComponent.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "FooterNoticeContractChecks.h"
#include <iostream>
#include <stdexcept>

namespace hypha::tests
{
inline void verifyAuCycleGuidanceUi()
{
    using namespace live_compare;
    const auto require = [] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); };
    const juce::String status = "Turn Cycle off to compare", listen = "Turn Cycle off to compare (POST)";
    const juce::String cause = "PRE/POST timing is unverified during Cycle.";
    const juce::String restart = "Turn Cycle off. Press END, then start BLIND again.";
    const std::pair<juce::String, const char*> translations[] {
        { status, u8"サイクルをオフにすると比べられます" },
        { listen, u8"サイクルをオフにすると比べられます（今はPOST）" },
        { cause, u8"サイクル中のPREとPOSTの対応を確認できません。" },
        { restart, u8"サイクルをオフにし、ENDの後にBLINDを始め直してください。" }
    };
    for (const auto& entry : translations)
        require (i18n::translate (entry.first, i18n::Language::japanese) == juce::String::fromUTF8 (entry.second),
                 "Cycle translations match the public instruction exactly");
    require (i18n::translate ("LISTEN: " + cause, i18n::Language::japanese)
        == juce::String::fromUTF8 (u8"LISTEN：サイクル中のPREとPOSTの対応を確認できません。"),
        "LISTEN details translate the composed cause including its punctuation");
    int surfaces = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (int width : { 300, 375, 450 })
        {
            const int height = width * 2 / 3;
            juce::Image review (juce::Image::RGB, width * 2, height * 3, true);
            juce::Graphics composite (review);
            composite.fillAll (juce::Colours::black);
            int panel = 0;
            const auto capture = [&] (juce::Component& component, const juce::String& sentence)
            {
                text_style::ShownTextLog drawn;
                const auto pixels = component.createComponentSnapshot (component.getLocalBounds());
                require (pixels.isValid(), "Cycle surface renders");
                if (sentence.isNotEmpty())
                    require (drawn.texts().joinIntoString ("").removeCharacters (" \n")
                        .contains (text_style::shownText (sentence).removeCharacters (" \n")),
                        "analysis Cycle instruction is drawn whole without ellipsis");
                composite.drawImageAt (pixels, (panel % 2) * width, (panel / 2) * height);
                ++panel; ++surfaces;
            };
            observatory::View shell (observatory::Role::post); shell.setVisible (true);
            shell.setSize (width, height);
            const auto context = shell.presentationContext();
            const auto body = shell.bodyBounds();
            SpectrumComponent freq;
            freq.setSize (body.getWidth(), body.getHeight()); freq.setPresentationContext (context);
            freq.setAbsoluteObservation (false); freq.setSignalActive (true);
            KirinSpectrumView frame {}; frame.status = KIRIN_SPECTRUM_WARMING_UP;
            freq.setSnapshot (frame); freq.setPairingLoopBlock (LoopPairingBlock::auCycle);
            capture (freq, status);
            PerceptualComponent sharp;
            sharp.setSize (body.getWidth(), body.getHeight()); sharp.setPresentationContext (context);
            KirinPerceptualView perceptual {}; perceptual.status = KIRIN_SPECTRUM_WARMING_UP;
            sharp.setSnapshot (perceptual); sharp.setSignalActive (true);
            sharp.setPairingLoopBlock (LoopPairingBlock::auCycle);
            capture (sharp, status);
            SpectrumComponent psb;
            psb.setSize (450, 300); psb.setPresentationContext (presentation::forEditor (450, 300));
            psb.setAbsoluteObservation (false); psb.setSignalActive (true);
            psb.setPairingLoopBlock (LoopPairingBlock::auCycle);
            const auto bounds = psb.getLocalBounds().toFloat();
            const auto point = spectrum_geometry::subviewBoundsFor (spectrum_geometry::plotBoundsFor (bounds),
                spectrum_geometry::visualScaleFor (bounds)).getCentre();
            const auto now = juce::Time::getCurrentTime();
            psb.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
                {}, 0, 0, 0, 0, 0, &psb, &psb, now, point, now, 0, false));
            require (psb.isPsbObservation(), "PSB selected with its real control");
            psb.setSize (body.getWidth(), body.getHeight()); psb.setPresentationContext (context);
            require (psb.psbStatusText() == status, "PSB and FREQ share the AU reason");
            capture (psb, status);
            Status named;
            named.active = named.preSelected = named.preWaiting = true;
            named.loopBlock = LoopPairingBlock::auCycle;
            named.observation = RecoveryReason::loopClockUnavailable; named.timingUnavailable = true;
            const auto notice = live_compare_ui::currentNamedPresentation (named);
            require (juce::String (notice.instruction) == listen && juce::String (live_compare_ui::cause (notice.reason)) == cause,
                     "LISTEN uses Cycle wording before the generic timing boundary");
            shell.setFeedback (notice.instruction);
            require (footer_notice::retainedInFooter (shell, listen), "LISTEN text fits and full details remain available");
            capture (shell, {});
            if (shell.statusStripFolded())
            {
                FeedbackStrip strip; strip.setBounds (shell.statusStripBounds());
                strip.setPresentationContext (context); strip.setFeedback (listen);
                const auto pixels = strip.createComponentSnapshot (strip.getLocalBounds());
                composite.drawImageAt (pixels, width + strip.getX(), height + strip.getY());
            }
            for (bool interrupted : { false, true })
            {
                LiveBlindStatus state;
                state.stage = interrupted ? BlindStage::invalidated : BlindStage::preparing;
                state.loopBlock = interrupted ? LoopPairingBlock::none : LoopPairingBlock::auCycle;
                state.reason = interrupted ? RecoveryReason::cycleUnverified : RecoveryReason::none;
                state.observation = interrupted ? RecoveryReason::none : RecoveryReason::loopClockUnavailable;
                state.timingUnavailable = ! interrupted;
                const auto screen = live_blind_ui::screenFor (state, true, 1.0f);
                require (screen.cause == cause && screen.recovery == (interrupted ? restart : status)
                    && screen.guidanceShown && screen.endEnabled && ! screen.sourcesShown && ! screen.approveShown,
                    "BLIND preserves Cycle reason after Cycle off; no anonymous output or approval");
                live_blind_ui::Component blind;
                blind.setSize (width, height); blind.setState (state, true, 1.0f);
                for (auto* child : blind.getChildren())
                {
                    if (! child->isVisible()) continue;
                    require (! child->getBounds().isEmpty() && blind.getLocalBounds().contains (child->getBounds()),
                             "Cycle controls fit at 100/125/150 percent");
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                    {
                        const auto room = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                        const auto needed = text_style::shownWrappedHeight (label->getText(), label->getFont(), room.getWidth());
                        if (needed > room.getHeight() + 0.5f)
                            std::cerr << "Cycle label overflow " << width << " " << static_cast<int> (language)
                                << " " << label->getText() << " needs " << needed << " in " << room.getHeight() << '\n';
                        require (needed <= room.getHeight() + 0.5f, "Cycle cause and recovery fit without clipping");
                    }
                }
                capture (blind, {});
            }
            // Cycle off returns to ordinary preparation. Other conditions retain their own text.
            psb.setPairingLoopBlock (LoopPairingBlock::none);
            require (psb.psbStatusText() == "PSB WARMING", "Cycle off restores ordinary preparation");
            psb.setPairingLoopBlock (LoopPairingBlock::auCycle); psb.setAbsoluteObservation (true);
            require (psb.psbStatusText() == "PSB WARMING", "absolute PSB is independent of pairing");
            named.loopBlock = LoopPairingBlock::none; named.timingUnavailable = false;
            named.observation = RecoveryReason::calibrating;
            require (juce::String (live_compare_ui::namedRecovery (named)) == "Checking PRE: auto-resume; POST plays",
                     "ordinary calibration is not Cycle guidance");
            named.clockUnavailable = true; named.observation = RecoveryReason::clockMissing;
            require (juce::String (live_compare_ui::namedRecovery (named)).startsWith ("This DAW has no verified audio clock"),
                     "host clock limitations are not Cycle guidance");
            const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
            if (directory.isNotEmpty())
            {
                auto out = juce::File (directory).getChildFile ("cycle-" + juce::String (width)
                    + (language == i18n::Language::english ? "-en.png" : "-ja.png")).createOutputStream();
                require (out && out->setPosition (0) && out->truncate().wasOk()
                    && juce::PNGImageFormat().writeImageToStream (review, *out), "save disposable Cycle UI review");
            }
        }
    }
    std::cout << "AU Cycle UI: " << surfaces << " language/size/surface cases PASS\n";
}
}
