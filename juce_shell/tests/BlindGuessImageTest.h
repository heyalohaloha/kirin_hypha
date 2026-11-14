#pragma once

// 2026-11-11：Blind の GUESS（当てる、既定 OFF）と BLIND RESULT の SAVE IMAGE。
// OFF は今の Blind のまま。ON は REVEAL の前に両方の割り当てで同じ画面、後に当たり／はずれ。
// 画像は名前欄の名前だけを使い、曲名・トラック名を入れず、画面の言語で、最後の一文と URL で終わる。
#include "../src/HyphaBlindImageSheet.h"
#include "../src/HyphaCapturePngFile.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "EditorReachabilityCheck.h"
#include "TestPathBudget.h"
#include "TestTemporaryDirectory.h"
#include <cmath>
#include <iostream>
#include <set>

namespace hypha::tests
{
namespace blind_guess_test
{
inline juce::StringArray& misses()
{
    static juce::StringArray seen;
    return seen;
}

inline live_compare::LiveBlindStatus trial (bool candidate, bool revealed, bool firstPre, int played = 3)
{
    live_compare::LiveBlindStatus state;
    state.stage = live_compare::BlindStage::active;
    state.candidate = candidate;
    state.generation = 7;
    state.trial.epoch = 3;
    state.trial.played = played;
    state.trial.selected = state.trial.audible = 2;
    state.trial.revealed = revealed;
    state.trial.firstPre = firstPre;
    return state;
}

inline bool samePixels (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
    return true;
}

template <typename Require>
void requireScreenFits (juce::Component& view, Require require, const juce::String& where)
{
    const auto context = presentation::forEditor (view.getWidth(), view.getHeight());
    for (int i = 0; i < view.getNumChildComponents(); ++i)
    {
        auto* child = view.getChildComponent (i);
        if (! child->isVisible()) continue;
        require (view.getLocalBounds().contains (child->getBounds()) && ! child->getBounds().isEmpty(), where + ": inside");
        for (int j = i + 1; j < view.getNumChildComponents(); ++j)
            require (! view.getChildComponent (j)->isVisible() || ! child->getBounds().intersects (view.getChildComponent (j)->getBounds()),
                     where + ": no overlap " + child->getComponentID() + " " + view.getChildComponent (j)->getComponentID());
        if (auto* action = dynamic_cast<HyphaTextButton*> (child))
            require (text_style::shownWidth (labelFont (context, typography::TextRole::action), action->displayedText(),
                                             text_style::LabelPolicy::fixed) <= action->getWidth() - 12, where + ": action fits");
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText().isNotEmpty())
        {
            const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
            require (text_style::shownWrappedHeight (label->getText(), label->getFont(), bounds.getWidth()) <= bounds.getHeight() + 0.5f,
                     where + ": text fits " + text_style::shownText (label->getText()) + " in " + bounds.toString() + " needs "
                         + juce::String (text_style::shownWidth (label->getFont(), label->getText())) + " x "
                         + juce::String (text_style::shownWrappedHeight (label->getText(), label->getFont(), bounds.getWidth())));
        }
    }
    editor_reachability::checkAtDisplayScales (view, [&] (float scale, const juce::String& problem)
    { require (false, where + " @" + juce::String (scale) + ": " + problem); });
}
}

template <typename Require>
void verifyBlindGuessAndImage (Require check)
{
    using namespace blind_guess_test;
    const auto require = [&] (bool ok, const juce::String& what) { check (ok, what.toRawUTF8()); };
    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_BLIND_IMAGE_PREVIEW_DIR", {});
    const auto root = hypha::tests::temporaryParent().getNonexistentChildFile ("b", {}, false);
    require (root.createDirectory().wasOk(), "test folder");
    const auto preferenceFixture = root.getChildFile ("u");

    // A mismatched typed extension must never bypass confirmation of the final PNG.
    {
        const auto png = root.getChildFile ("result.png"), jpg = root.getChildFile ("result.jpg");
        require (! capture::replacePngApproved (0) && capture::replacePngApproved (1)
            && ! capture::replacePngApproved (2) && ! capture::replacePngApproved (-1), "only Replace approves");
        require (png.replaceWithText ("original"), "existing PNG fixture");
        int confirmations = 0, saves = 0;
        std::function<void (bool)> reply;
        const auto confirm = [&] (const juce::File& target, std::function<void (bool)> answer)
        { ++confirmations; require (target == png, "confirm the normalized PNG"); reply = std::move (answer); };
        const auto save = [&] (const juce::File& target)
        { ++saves; require (target == png, "save the confirmed PNG"); };
        capture::choosePngTarget (jpg, confirm, save);
        require (confirmations == 1 && saves == 0 && png.loadFileAsString() == "original", "existing PNG waits for confirmation");
        reply (false);
        require (saves == 0 && png.loadFileAsString() == "original", "cancel keeps the PNG");
        capture::choosePngTarget (jpg, confirm, save);
        reply (true);
        require (confirmations == 2 && saves == 1, "approval saves the same PNG");
        const auto fresh = root.getChildFile ("fresh.jpg");
        require (fresh.replaceWithText ("unrelated"), "other extension fixture");
        capture::choosePngTarget (fresh, [&] (const juce::File&, std::function<void (bool)>) { require (false, "no PNG to replace"); },
            [&] (const juce::File& target) { require (target == fresh.withFileExtension (".png"), "new PNG uses normalized path"); });
        capture::choosePngTarget ({}, confirm, save);
        require (confirmations == 2 && saves == 1, "cancelled chooser does nothing");
    }

    // OFF: the screen is the Blind it was, whatever answer is held.
    for (bool candidate : { false, true })
        for (bool revealed : { false, true })
            for (bool firstPre : { false, true })
            {
                const auto state = trial (candidate, revealed, firstPre);
                require (live_blind_ui::screenFor (state, true, 1.0f) == live_blind_ui::screenFor (state, true, 1.0f, false, { false, 2 })
                    && ! live_blind_ui::screenFor (state, true, 1.0f).guessShown, "GUESS off adds nothing");
                require (live_blind_ui::screenFor (state, true, 1.0f).saveImageShown == revealed, "SAVE IMAGE only on the RESULT");
            }

    // ON before REVEAL: the same screen for both assignments; after REVEAL, right or not this time.
    for (bool candidate : { false, true })
    {
        const juce::String question = candidate ? "Which one is B?" : "Which one is POST?";
        for (int answer : { 0, 1, 2 })
        {
            const auto one = live_blind_ui::screenFor (trial (candidate, false, true), true, 1.0f, false, { true, answer });
            require (one == live_blind_ui::screenFor (trial (candidate, false, false), true, 1.0f, false, { true, answer })
                && one.guessShown && one.guessOpen && one.guessQuestion == question && one.guessOutcome.isEmpty(),
                "before REVEAL the question never depends on the assignment");
            require (one.revealEnabled == (answer != 0), "GUESS requires an answer before REVEAL");
            for (bool firstPre : { false, true })
            {
                const auto shown = live_blind_ui::screenFor (trial (candidate, true, firstPre), true, 1.0f, false, { true, answer });
                const bool right = answer == (firstPre ? 2 : 1);
                require (shown.guessShown == (answer != 0) && ! shown.guessOpen, "after REVEAL only an answer stays");
                if (answer != 0)
                    require (shown.guessRight == right && shown.guessOutcome == juce::String::fromUTF8 (right ? u8"— right" : u8"— not this time"),
                             "REVEAL tells right or not this time");
            }
        }
        const auto stopped = [&] { auto s = trial (candidate, false, true); s.stage = live_compare::BlindStage::invalidated; return s; }();
        require (! live_blind_ui::screenFor (stopped, true, 1.0f, false, { true, 1 }).guessShown, "a stopped Blind asks nothing");
    }

    // The component: pixels before REVEAL are identical for both assignments; answers reset per trial.
    live_blind_ui::Component view;
    view.setPreferenceFile (preferenceFixture);
    view.setSize (450, 300);
    view.setGuessing (true);
    view.setState (trial (true, false, true), true, 1.0f);
    bool revealedByAction = false;
    view.onReveal = [&] { revealedByAction = true; };
    require (! view.requestReveal() && ! revealedByAction, "REVEAL cannot bypass a missing GUESS answer");
    dynamic_cast<juce::Button*> (view.findChildWithID ("live-blind-guess-2"))->onClick();
    require (view.requestReveal() && revealedByAction, "REVEAL works after choosing an answer");
    const auto first = view.createComponentSnapshot (view.getLocalBounds());
    view.setState (trial (true, false, false), true, 1.0f);
    require (samePixels (first, view.createComponentSnapshot (view.getLocalBounds())), "BLIND never shows the assignment before REVEAL");
    view.setState (trial (true, true, false), true, 1.0f);
    require (view.screen().guessOutcome == juce::String::fromUTF8 (u8"— not this time"), "the held answer is judged at REVEAL");
    auto next = trial (true, false, true);
    next.trial.epoch = 4;
    view.setState (next, true, 1.0f);
    require (view.screen().guessAnswer == 0, "a new trial starts without an answer");

    // Every size and both languages: the question row, the RESULT and the sheet fit and are reachable.
    juce::String tooWide;
    // Labels stay English (INV-S40); prose with lower-case words must reach Japanese.
    i18n::observeMisses ([] (const juce::String& text)
    { if (text.containsAnyOf ("abcdefghijklmnopqrstuvwxyz")) misses().addIfNotAlreadyThere (text); });
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        const juce::String tag = language == i18n::Language::japanese ? "JA " : "EN ";
        for (const auto& preset : observatory::sizePresets)
            for (bool candidate : { false, true })
                for (int phase = 0; phase < 3; ++phase)
                {
                    view.closeImageSheet();
                    view.setSize (preset.width, preset.height);
                    view.setState (trial (candidate, false, true, phase == 0 ? 1 : 3), true, 1.0f);
                    if (phase > 0) view.onGuess (phase);
                    if (phase == 2) view.setState (trial (candidate, true, true), true, 0.9f);
                    requireScreenFits (view, require, tag + juce::String (preset.width) + " phase " + juce::String (phase));
                    if (phase != 2) continue;
                    require (view.findChildWithID ("live-blind-save-image")->isVisible(), "SAVE IMAGE stands where REVEAL stood");
                    view.openImageSheet();
                    require (view.imageSheetOpen(), "SAVE IMAGE opens the names");
                    requireScreenFits (view.imageSheet(), require, tag + juce::String (preset.width) + " sheet");
                }
        // On a RESULT with an ongoing alignment warning, saving and ALIGN OFF remain reachable.
        for (const auto& preset : observatory::sizePresets)
            for (bool guessing : { false, true })
            {
                view.closeImageSheet();
                view.setSize (preset.width, preset.height);
                view.setGuessing (guessing);
                auto held = trial (true, false, true);
                held.prolongedWait = true;
                held.observation = live_compare::RecoveryReason::contentAmbiguous;
                view.setState (held, true, 1.0f);
                if (guessing) view.onGuess (1);
                held.trial.revealed = true;
                view.setState (held, true, 1.0f);
                require (view.findChildWithID ("live-blind-save-image")->isVisible()
                    && view.findChildWithID ("live-blind-align-off")->isVisible(), "save and alignment actions coexist");
                requireScreenFits (view, require, tag + juce::String (preset.width) + " result with alignment warning");
            }
        view.setGuessing (true);
        // Hover help of the new controls and the GUESS notice fit the footer line at 300% (the same font as
        // the help line). A failed save's reason is on the sheet, where it wraps (checked below).
        observatory::View shell (observatory::Role::post);
        shell.setSize (900, 600);
        const auto line = shell.statusStripBounds().reduced (4, 0);
        const auto font = labelFont (presentation::forEditor (900, 600), typography::TextRole::unit, typography::Composition::information);
        for (const auto* help : { "Answer SOURCE 1", "Answer SOURCE 2", "Choose an answer first", "Reveal the sources after choosing an answer", "The answer was fixed at REVEAL", "Save the result as an image",
                                  "Choose where to save the image", "Back to the result", "Name of POST in the image, up to 16 characters",
                                  "GUESS setting could not be saved", "Image saved", "Names could not be remembered",
                                  "Image saved; names could not be remembered" })
            if (text_style::shownWidth (font, help) > (float) line.getWidth())
                tooWide << "\n  " << tag << juce::String (text_style::shownWidth (font, help), 0) << " > " << line.getWidth()
                        << ": " << text_style::shownText (help);
    }

    require (tooWide.isEmpty(), "every new help fits the footer line at 300%:" + tooWide);

    // Names: 16 characters, one line, the default when empty, remembered per Blind.
    require (blind_image::cleanName ("  \tABCDEFGHIJKLMNOPQRST\n", "A") == "ABCDEFGHIJKLMNOP", "16 characters, one line");
    require (blind_image::cleanName ("   ", "POST") == "POST", "empty uses the default");
    require (blind_image::rememberedName (preferenceFixture, true, true) == "B", "A/B starts as A and B");
    require (blind_image::rememberedName (preferenceFixture, false, false) == "PRE", "PRE/POST starts as PRE and POST");
    {
        live_blind_ui::Component closing;
        closing.setPreferenceFile (preferenceFixture);
        closing.setSize (450, 300);
        closing.setState (trial (true, true, true), true, 1.0f);
        closing.openImageSheet();
        closing.imageSheet().nameField (false).setText ("Kept on close", true);
    }
    require (blind_image::rememberedName (preferenceFixture, true, false) == "Kept on close", "closing the editor remembers an entered name");
    {
        live_blind_ui::Component unable;
        const auto blocked = root.getChildFile ("blocked");
        require (blocked.createDirectory().wasOk(), "blocked preferences fixture");
        unable.setPreferenceFile (blocked); // a directory cannot be replaced by the preferences file
        unable.setSize (450, 300);
        unable.setState (trial (true, true, true), true, 1.0f);
        unable.openImageSheet();
        unable.closeImageSheet();
        require (unable.actionNoticeShown() == "Names could not be remembered", "name persistence failure is reported");
        unable.openImageSheet();
        require (unable.saveImageTo (root.getChildFile ("saved.png"))
            && unable.actionNoticeShown() == "Image saved; names could not be remembered",
            "the image still saves when names cannot be remembered");
    }
    {
        auto state = trial (true, true, false);
        state.trial.usedPostFallback = true;
        view.setSize (600, 400);
        view.setState (state, true, 1.0f);
        view.onGuess (1);  // closed after REVEAL: ignored by the screen, kept off the image
        view.openImageSheet();
        view.imageSheet().nameField (false).setText (juce::String::fromUTF8 (u8"実機のコンプレッサー一号機です"), true);
        view.imageSheet().nameField (true).setText ("", true);
        view.closeImageSheet();
        require (blind_image::rememberedName (preferenceFixture, true, false) == juce::String::fromUTF8 (u8"実機のコンプレッサー一号機です")
            && blind_image::rememberedName (preferenceFixture, true, true) == "B"
            && blind_image::rememberedName (preferenceFixture, false, false) == "PRE", "names are remembered for this Blind only");

        // Saving: a reason on the sheet when it fails, the PNG when it does not. The reason fits at 300 x 200.
        view.setSize (300, 200);
        view.openImageSheet();
        const auto folder = root.getChildFile ("images");
        require (folder.createDirectory().wasOk(), "image folder");
        require (! view.saveImageTo (folder) && view.imageSheet().reasonShown() == "The file could not be replaced; choose another name",
                 "a failed save says why");
        for (auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage shown (language);
            view.imageSheet().resized();
            for (const auto* reason : { "The image could not be made; try again", "This folder cannot be written; choose another",
                                        "The file could not be replaced; choose another name", "The file could not be written; check free space" })
            {
                view.imageSheet().setReason (reason);
                requireScreenFits (view.imageSheet(), require, "failed save reason");
            }
        }
       #if ! JUCE_WINDOWS
        const auto locked = root.getChildFile ("locked");
        require (locked.createDirectory().wasOk() && locked.setReadOnly (true, false), "locked folder");
        require (! view.saveImageTo (locked.getChildFile ("x.png"))
            && view.imageSheet().reasonShown() == "This folder cannot be written; choose another", "a locked folder says so");
        locked.setReadOnly (false, false);
       #endif
        const auto target = folder.getChildFile ("result.png");
        require (view.saveImageTo (target) && ! view.imageSheetOpen() && view.actionNoticeShown() == "Image saved", "saved");
        const auto saved = juce::ImageFileFormat::loadFrom (target);
        require (saved.getWidth() == blind_image::pixelSize && saved.getHeight() == blind_image::pixelSize, "a 1080 square PNG");
    }

    // The image: only the RESULT's facts and the typed names, in the screen's language, ending with
    // the one sentence and the address; every line fits its place and nothing overlaps.
    const juce::String longName = juce::String::fromUTF8 (u8"ＷＷＷＷＷＷＷＷＷＷＷＷＷＷＷＷ");
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        blind_image::Facts namedNote;
        namedNote.nameA = "POST box"; namedNote.nameB = "PRE box";
        namedNote.note = "POST played where PRE went over the ceiling";
        const auto expectedNote = language == i18n::Language::english
            ? juce::String ("PRE box played where POST box went over the ceiling")
            : juce::String::fromUTF8 (u8"POST boxが上限を超えた所はPRE boxを鳴らしました");
        require (blind_image::detail::noteWithNames (namedNote) == expectedNote, "result notes use names without rewriting them");
        namedNote.candidate = true;
        namedNote.note = "Audio estimate: A may have been 12.34 ms off";
        const auto expectedEstimate = language == i18n::Language::english
            ? juce::String ("Audio estimate: POST box may have been 12.34 ms off")
            : juce::String::fromUTF8 (u8"音の推定：POST boxが12.34 msずれていた可能性");
        require (blind_image::detail::noteWithNames (namedNote) == expectedEstimate, "A/B estimate uses the typed name");
        if (language == i18n::Language::japanese)
        {
            blind_image::Facts literalName;
            literalName.nameA = "Selected"; literalName.nameB = "B";
            text_style::ShownTextLog painted;
            blind_image::render (literalName);
            require (painted.texts().contains ("Selected"), "an entered name is not translated during drawing");
        }
        for (int variant = 0; variant < 6; ++variant)
        {
            blind_image::Facts facts;
            facts.candidate = variant != 1;
            facts.firstPre = variant % 2 == 0;
            facts.selected = variant % 2 + 1;
            facts.guess = variant == 3 ? 0 : variant == 2 ? 1 : 2;
            facts.loweredDb = variant >= 4 ? 2.0 : 0.0;
            facts.note = variant >= 4 ? "Chain audio was used briefly / Audio estimate: A may have been 12.34 ms off" : "";
            facts.nameA = variant == 5 ? longName : variant == 4 ? "WWWWWWWWWWWWWWWW" : variant == 0 ? "Hardware" : "A";
            facts.nameB = variant == 5 ? "Plug-in with a l" : variant == 0 ? "Plug-in" : facts.candidate ? "B" : "POST";
            const auto layout = blind_image::layoutFor (facts);
            const auto& items = layout.items;
            const juce::Rectangle<float> canvas (0.0f, 0.0f, blind_image::logicalSize, blind_image::logicalSize);
            require (BG.isOpaque() && layout.badge.contains (items[1].area), "the blind label has a solid dark backing");
            if (language == i18n::Language::japanese)
            {
                const blind_image::Item* latin = nullptr;
                const blind_image::Item* other = nullptr;
                const blind_image::Item* selected = nullptr;
                for (const auto& item : items)
                {
                    if (item.role == typography::BlindImageText::small && item.text == (facts.selected == 1 ? "SOURCE 1 / " : "SOURCE 2 / ")) latin = &item;
                    if (item.role == typography::BlindImageText::small && item.text == (facts.selected == 1 ? "SOURCE 2" : "SOURCE 1")) other = &item;
                    if (item.text == i18n::tr ("Selected")) selected = &item;
                }
                require (latin != nullptr && other != nullptr && selected != nullptr, "Japanese selected caption has separate font runs");
                if (latin != nullptr && other != nullptr && selected != nullptr)
                    require (latin->font == other->font && selected->font.isBold()
                        && std::abs (latin->font.getHeight() - selected->font.getHeight()) < 0.01f
                        && std::abs (latin->area.getRight() - selected->area.getX()) < 0.01f, "matching source font and weighted Japanese suffix");
            }
            require (items.size() >= 2 && items.back().text == blind_image::address
                && items[items.size() - 2].text == i18n::tr (blind_image::closingSentence), "ends with the sentence and the address");
            if (language == i18n::Language::japanese)
                require (items[items.size() - 2].text == juce::String::fromUTF8 (u8"音量をそろえて 1 回聴いた結果です。"), "Japanese sentence");
            std::set<juce::String> allowed { "KIRIN HYPHA", "A/B BLIND", "PRE/POST BLIND", "BLIND RESULT", "SOURCE 1", "SOURCE 2",
                                             "SOURCE 1 / ", "SOURCE 2 / ", i18n::tr ("Selected"),
                                             i18n::tr ("SOURCE 1") + " / " + i18n::tr ("Selected"),
                                             i18n::tr ("SOURCE 2") + " / " + i18n::tr ("Selected"),
                                             blind_image::address, i18n::tr (blind_image::closingSentence),
                                             i18n::tr (juce::String::fromUTF8 (u8"— right")), i18n::tr (juce::String::fromUTF8 (u8"— not this time")),
                                             i18n::tr ("Which one is %1?").replace ("%1", facts.nameB) };
            juce::String prose;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const auto& item = items[i];
                require (item.colour.isOpaque() && item.colour != COL_FLORA, "image text is opaque and not orange");
                require (canvas.contains (item.area) && item.font.getStringWidthFloat (item.text) <= item.area.getWidth() + 0.5f
                    && item.font.getHeight() * 2.0f >= 24.0f, "every line fits whole, at 12 px or more");
                for (size_t j = i + 1; j < items.size(); ++j)
                    require (! item.area.intersects (items[j].area), "image lines overlap: " + juce::String (variant) + " "
                        + item.text + " / " + items[j].text + " " + item.area.toString() + " / " + items[j].area.toString());
                const bool name = facts.nameA.contains (item.text) || facts.nameB.contains (item.text);
                if (! name && allowed.count (item.text) == 0) prose << item.text << " ";
                if (i + 2 < items.size()) require (item.area.getBottom() <= layout.rule, "everything else stands above the closing");
            }
            const auto notes = (facts.loweredDb > 0.0 ? i18n::tr ("Lowered 2.0 dB to avoid clipping") : juce::String())
                + blind_image::detail::noteWithNames (facts);
            require (prose.removeCharacters (" ") == notes.removeCharacters (" "), "image facts " + juce::String (variant)
                + ": " + prose + " != " + notes);
            if (preview.isNotEmpty() && (variant == 0 || variant == 4))
            {
                const auto file = juce::File (preview).getChildFile ("blind-result-" + juce::String (variant == 0 ? "" : "notes-")
                    + (language == i18n::Language::japanese ? "ja" : "en") + ".png");
                file.deleteFile();
                require (capture::writePng (blind_image::render (facts), file) == capture::PngSave::saved, "preview written");
            }
        }
    }
    i18n::observeMisses (nullptr);
    require (misses().isEmpty(), "every new Japanese line is in the catalog: " + misses().joinIntoString (" | "));
    require (hypha::tests::removeFixtureDirectory (root), "remove the isolated image fixture");
    std::cout << "Blind GUESS and SAVE IMAGE: PASS\n";
}
}
