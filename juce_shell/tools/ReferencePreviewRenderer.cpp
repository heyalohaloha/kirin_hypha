#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <iostream>

namespace
{
using hypha::reference_ui::SelectionOption;

struct Failure
{
    const char* code;
};

[[noreturn]] void fail (const char* code)
{
    throw Failure { code };
}

juce::DynamicObject* object (const juce::var& value)
{
    auto* result = value.getDynamicObject();
    if (result == nullptr)
        fail ("reference-preview-request-invalid");
    return result;
}

bool exactProperties (const juce::DynamicObject& value,
                      std::initializer_list<const char*> names)
{
    if (value.getProperties().size() != static_cast<int> (names.size()))
        return false;
    for (const auto* name : names)
        if (! value.hasProperty (name))
            return false;
    return true;
}

void requireExactProperties (const juce::DynamicObject& value,
                             std::initializer_list<const char*> names)
{
    if (! exactProperties (value, names))
        fail ("reference-preview-request-invalid");
}

juce::int64 integer (juce::DynamicObject* value, const char* key)
{
    const auto property = value->getProperty (key);
    if (! property.isInt() && ! property.isInt64())
        fail ("reference-preview-request-invalid");
    return static_cast<juce::int64> (property);
}

juce::String text (juce::DynamicObject* value, const char* key, int maximum = 160)
{
    const auto property = value->getProperty (key);
    if (! property.isString())
        fail ("reference-preview-request-invalid");
    const auto result = property.toString();
    if (result.isEmpty() || result.length() > maximum)
        fail ("reference-preview-request-invalid");
    return result;
}

std::vector<SelectionOption> options (juce::DynamicObject* value)
{
    const auto property = value->getProperty ("options");
    const auto* array = property.getArray();
    if (array == nullptr || array->isEmpty() || array->size() > 64)
        fail ("reference-preview-request-invalid");
    std::vector<SelectionOption> output;
    output.reserve (static_cast<size_t> (array->size()));
    for (const auto& entry : *array)
    {
        auto* item = object (entry);
        requireExactProperties (*item, { "id", "label" });
        const auto id = text (item, "id", 128);
        if (std::any_of (output.begin(), output.end(), [&id] (const auto& option)
            { return option.id == id; }))
            fail ("reference-preview-request-invalid");
        output.push_back ({ id, text (item, "label") });
    }
    return output;
}

std::vector<juce::String> viewBindings (juce::DynamicObject* check)
{
    const auto property = check->getProperty ("view_bindings");
    const auto* array = property.getArray();
    if (array == nullptr || array->size() > 3)
        fail ("reference-preview-request-invalid");
    std::vector<juce::String> output;
    const std::vector<juce::String> allowed {
        "waveform", "spectrum_full", "spectrum_low", "loudness",
        "dynamics", "transient", "stereo"
    };
    for (const auto& entry : *array)
    {
        const auto value = entry.toString();
        if (std::find (allowed.begin(), allowed.end(), value) == allowed.end()
            || std::find (output.begin(), output.end(), value) != output.end())
            fail ("reference-preview-request-invalid");
        output.push_back (value);
    }
    return output;
}

hypha::reference_ui::State stateFromRequest (juce::DynamicObject* root)
{
    requireExactProperties (*root, {
        "schema_version", "locale", "width", "height", "example",
        "preset", "check", "candidate", "cue"
    });
    if (text (root, "schema_version") != "kirin_hypha_reference_preview_request.v1")
        fail ("reference-preview-request-invalid");
    const auto locale = text (root, "locale", 2);
    if ((locale != "ja" && locale != "en")
        || integer (root, "width") != 900
        || integer (root, "height") != 600)
        fail ("reference-preview-request-invalid");
    const auto example = text (root, "example", 16);
    if (example != "normal" && example != "blind")
        fail ("reference-preview-request-invalid");
    auto* preset = object (root->getProperty ("preset"));
    auto* check = object (root->getProperty ("check"));
    auto* candidate = object (root->getProperty ("candidate"));
    auto* cue = object (root->getProperty ("cue"));
    requireExactProperties (*preset, { "id", "label", "options" });
    requireExactProperties (*check, {
        "id", "label", "options", "comparison_mode", "view_bindings"
    });
    requireExactProperties (*candidate, { "id", "label", "options" });
    requireExactProperties (*cue, { "id", "label", "options" });

    hypha::reference_ui::State state;
    state.readiness = hypha::reference_ui::Readiness::ready;
    state.osAccess = hypha::os_access::State::ready;
    state.auditionBuffered = true;
    state.title = text (candidate, "label");
    state.sourceLabel = "REFERENCE PREVIEW";
    state.status = "PREVIEW / DAW INPUT NOT AVAILABLE";
    state.separateComparisons = true;
    state.alignmentLabel = "REFERENCE CUE";
    state.presetId = text (preset, "id", 128);
    state.presetName = text (preset, "label");
    state.presets = options (preset);
    const auto checkId = text (check, "id", 128);
    state.checkLabel = text (check, "label");
    const auto checkOptions = options (check);
    state.comparisonMode = text (check, "comparison_mode", 32);
    state.viewBindings = viewBindings (check);
    state.candidateId = text (candidate, "id", 128);
    state.candidateName = text (candidate, "label");
    state.candidates = options (candidate);
    state.cueId = text (cue, "id", 128);
    state.cueLabel = text (cue, "label");
    state.cues = options (cue);
    const auto hasSelectedId = [] (const auto& selected, const auto& choices)
    {
        return std::any_of (choices.begin(), choices.end(), [&selected] (const auto& option)
            { return option.id == selected; });
    };
    if (! hasSelectedId (state.presetId, state.presets)
        || ! hasSelectedId (checkId, checkOptions)
        || ! hasSelectedId (state.candidateId, state.candidates)
        || ! hasSelectedId (state.cueId, state.cues))
        fail ("reference-preview-request-invalid");
    // H15: ABCV の C（CHECK）の画面（Kirin OS の CHECK の編集画面が「Hypha ではこう見える」を出す）。Check の
    // タブは CHECK SET の Check、曲は選んでいる Check の曲（ほかの Check の曲は要求に無いので、今の曲で並べる）。
    for (const auto& option : checkOptions)
        if (option.id == checkId)
            for (const auto& song : state.candidates)
                state.checks.push_back ({ option.id + "/" + song.id, option.label + "  /  " + song.label });
        else
            state.checks.push_back ({ option.id + "/" + state.candidateId, option.label + "  /  " + state.candidateName });
    state.checkId = checkId + "/" + state.candidateId;
    state.comparisonSlot = 2;
    state.libraryReceived = state.osOnline = true;  // Kirin OS の中のプレビュー。Kirin OS を開く案内は出さない
    state.blindLargeScreen = true;
    // C が聴ける状態として描く（準備の段階と始め方の案内は Hypha 本体が言う。案内で C の画面を覆わない）。
    state.checkStep = hypha::reference_ui::SourceStep::ready;
    state.aAvailable = state.checkReady = state.checkArmable = true;
    state.versionStep = hypha::reference_ui::SourceStep::chooseVersion;
    if (example == "blind")
    {
        // Blind は V（VERSION）の画面から始まる（VERSION BLIND）。表示の例だけで、始めも記録もしない。
        state.comparisonSlot = 1;
        state.blindPhase = hypha::reference_ui::BlindPhase::active;
        state.transportPlaying = true;
        state.status = "BLIND / SOURCE IDENTITY HIDDEN";
    }
    else state.blindPhase = hypha::reference_ui::BlindPhase::unavailable;
    return state;
}

juce::Image render (hypha::reference_ui::State state, bool japanese)
{
    // 要求の locale（Kirin OS の画面の言語）で描く。Hypha の画面の言語の設定は変えない。
    const hypha::i18n::ScopedLanguage language (japanese ? hypha::i18n::Language::japanese : hypha::i18n::Language::english);
    juce::Component surface;
    hypha::observatory::View shell { hypha::observatory::Role::post };
    hypha::reference_ui::Component reference;
    surface.setSize (900, 600);
    reference.setPresentationContext (hypha::presentation::forOutput (
        900, 600, hypha::presentation::OutputTarget::referencePreview));
    shell.setBounds (surface.getLocalBounds());
    shell.setDomain (hypha::observatory::Domain::reference);
    shell.setConnection ("KIRIN OS PREVIEW", hypha::COL_LED_BLUE,
                         hypha::observatory::ConnectionState::paired);
    surface.addAndMakeVisible (shell);
    reference.setBounds (shell.bodyBounds());
    reference.setState (std::move (state));
    surface.addAndMakeVisible (reference);
    juce::Image image (juce::Image::ARGB, surface.getWidth(), surface.getHeight(), true);
    juce::Graphics graphics (image);
    surface.paintEntireComponent (graphics, true);
    return image;
}
}

int main (int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI initialise;
        if (argc != 3)
            fail ("reference-preview-arguments-invalid");
        const juce::File input { juce::String::fromUTF8 (argv[1]) };
        const juce::File output { juce::String::fromUTF8 (argv[2]) };
        if (! input.existsAsFile() || input.getSize() < 2 || input.getSize() > 65'536)
            fail ("reference-preview-request-invalid");
        const auto request = juce::JSON::parse (input.loadFileAsString());
        if (request.isVoid())
            fail ("reference-preview-request-invalid");
        if (output.existsAsFile() && ! output.deleteFile())
            fail ("reference-preview-output-failed");
        juce::FileOutputStream stream { output };
        if (! stream.openedOk()
            || ! juce::PNGImageFormat().writeImageToStream (
                render (stateFromRequest (object (request)), request["locale"] == juce::var ("ja")), stream))
            fail ("reference-preview-output-failed");
        std::cout << "{\"renderer_version\":\"1.0.0\"}\n";
        return EXIT_SUCCESS;
    }
    catch (const Failure& failure)
    {
        std::cerr << failure.code << '\n';
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "reference-preview-render-failed\n";
        return EXIT_FAILURE;
    }
}
