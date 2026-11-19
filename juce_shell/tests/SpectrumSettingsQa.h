#pragma once
// Test-only range runner. Counts refer to settings tuples, never all host/audio states.
#include "SpectrumSettingsTuples.h"
#include "SpectrumOptionEvidence.h"
#include <cstring>
namespace hypha::tests::settings_qa {
using namespace freq_paced;
inline int integer(const char* raw) {
    const juce::String text(raw);
    require(text.isNotEmpty() && text.containsOnly("0123456789"), "expected unsigned integer");
    const auto value = text.getLargeIntValue();
    require(value <= 2000000, "integer exceeds runner limit"); return static_cast<int>(value);
}
inline juce::String choices(const Tuple& tuple) {
    juce::String result;
    for (int f = 0; f < 8; ++f) result += juce::String(tuple.values.selected[static_cast<size_t>(f)]) + ":";
    return result + juce::String(static_cast<int>(tuple.average));
}
inline juce::String imageHash(const juce::Image& image) {
    juce::MemoryOutputStream bytes;
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < image.getHeight(); ++y)
        bytes.write(pixels.getLinePointer(y), static_cast<size_t>(image.getWidth() * pixels.pixelStride));
    return juce::SHA256(bytes.getData(), bytes.getDataSize()).toHexString();
}
template<class View> inline auto repaintCount(const View& view, int)
    -> decltype(static_cast<long long>(view.interactionRepaintsForTest())) {
    return static_cast<long long>(view.interactionRepaintsForTest());
}
template<class View> inline long long repaintCount(const View&, long) { return -1; }
struct ScreenResult { double cpuMs = 0, wallMs = 0; juce::String arrays, pixels; bool immediate = false; };
inline ScreenResult screen(const Tuple& tuple, int width, const Input& input) {
    SpectrumComponent view; freq_options::prepare(view, width);
    view.setDisplaySettings(tuple.values); view.setAverageChoice(tuple.average);
    require(view.displaySettingsForTest() == tuple.values && view.averageChoice() == tuple.average,
            "view did not adopt tuple");
    require(!view.frameDriverActiveForTest(), "virtual screening must not use VBlank");
    const auto bounds = view.getLocalBounds().toFloat();
    const auto plot = spectrum_geometry::dataPlotBoundsFor(bounds, false, tuple.values.rangeDb(), false);
    require(std::isfinite(plot.getX()) && std::isfinite(plot.getY())
        && std::isfinite(plot.getWidth()) && std::isfinite(plot.getHeight())
        && !plot.isEmpty() && bounds.contains(plot), "data plot outside component bounds");
    // Four fixed input frames, virtual elapsed time; not a steady-state CPU claim.
    for (int i = 0; i < 4; ++i) {
        auto frame = input.frames[static_cast<size_t>(i)];
        unsigned char original[sizeof(KirinSpectrumView)]; std::memcpy(original, &frame, sizeof(frame));
        view.queueSnapshot(frame, 1);
        const auto now = view.curveUpdateTimeForTest();
        view.presentationTickAt(now + 1000.0 / 30.0);
        require(std::memcmp(original, &frame, sizeof(frame)) == 0, "queue/tick changed input bytes");
    }
    const auto& latest = input.frames[3];
    const auto before = freq_pixels::arraysFor(view, latest);
    const auto lastTick = view.curveUpdateTimeForTest();
    juce::Image picture(juce::Image::ARGB, width, width * 2 / 3, true);
    ThreadCpu cpu; const auto cpuStart = cpu.seconds(); const auto wallStart = Clock::now();
    freq_options::paint(view, picture);
    ScreenResult result; result.cpuMs = 1000 * (cpu.seconds() - cpuStart);
    result.wallMs = ms(Clock::now() - wallStart);
    require(std::isfinite(result.cpuMs) && result.cpuMs >= 0 && std::isfinite(result.wallMs),
            "invalid paint timing");
    const auto after = freq_pixels::arraysFor(view, latest);
    require(before.arraysHash == after.arraysHash && lastTick == view.curveUpdateTimeForTest(),
            "paint mutated presented arrays or tick clock");
    result.arrays = after.arraysHash; result.pixels = imageHash(picture);
    // Interaction does not advance the model. Older baseline headers lack the request counter;
    // mark that boundary unverified rather than treating the absence as a PASS.
    const auto pointer = plot.getCentre();
    const juce::MouseEvent move {juce::Desktop::getInstance().getMainMouseSource(), pointer,
        {}, 0, 0, 0, 0, 0, &view, &view, {}, pointer, {}, 0, false};
    const auto repaintBefore = repaintCount(view, 0);
    view.mouseMove(move);
    result.immediate = repaintBefore >= 0;
    if (result.immediate) require(repaintCount(view, 0) > repaintBefore, "hover repaint delayed");
    view.mouseExit(move);
    require(lastTick == view.curveUpdateTimeForTest()
        && freq_pixels::arraysFor(view, latest).arraysHash == after.arraysHash,
        "hover changed model data or clock");
    require(view.displaySettingsForTest() == tuple.values && view.averageChoice() == tuple.average,
            "paint mutated chosen settings");
    return result;
}
// --freq-settings-qa roundtrip|screen|choice-roundtrip|choice-screen|cpu OUT SOURCE START COUNT WIDTH FPS MAX_SECONDS PREVIOUS_JSON_OR_DASH
inline int main(int argc, char** argv) {
    require(argc == 11, "settings QA expects mode/out/source/start/count/width/fps/max-seconds/previous");
    const juce::String mode(argv[2]), source(argv[4]);
    const bool choiceMode = mode == "cpu" || mode == "choice-roundtrip" || mode == "choice-screen";
    const bool screenMode = mode == "screen" || mode == "choice-screen";
    require(choiceMode || mode == "roundtrip" || mode == "screen", "unknown settings QA mode");
    const int start = integer(argv[5]), count = integer(argv[6]), width = integer(argv[7]);
    const int rate = integer(argv[8]), limitSeconds = integer(argv[9]);
    const auto ids = singleChoiceIds();
    const int total = choiceMode ? static_cast<int>(ids.size()) : population;
    require(count > 0 && start < total && count <= total - start, "range outside declared population");
    require(width == 600 || width == 900, "screen/CPU supports documented 600/900 widths");
    require(rate == 20 || rate == 30, "only baseline30/candidate20 pacing is supported");
    require(limitSeconds > 0 && limitSeconds <= 7200, "wall budget must be 1..7200 seconds");
    require(source.isNotEmpty() && !phaseProfileBuild, "source ID and nonprofile build required");
    require(juce::MessageManager::getInstance()->isThisTheMessageThread(), "requires GUI message thread");
    const juce::File output(argv[3]);
    require(!output.exists(), "new result directory required");
    const auto input = makeInput();
    if (juce::String(argv[10]) != "-") {
        const auto old = juce::JSON::parse(juce::File(argv[10]));
        require(old.isObject() && old.getProperty("schema", {}) == juce::var("spectrum-settings-qa-v1")
            && old.getProperty("source", {}) == juce::var(source)
            && old.getProperty("fixture", {}) == juce::var(input.sha256)
            && old.getProperty("mode", {}) == juce::var(mode)
            && static_cast<int>(old.getProperty("width", 0)) == width
            && static_cast<int>(old.getProperty("fps", 0)) == rate
            && static_cast<int>(old.getProperty("next", -1)) == start,
            "resume provenance/range mismatch");
    } else require(start == 0, "nonzero start requires previous summary");
    require(output.createDirectory().wasOk(), "cannot create QA result directory");
    const i18n::ScopedLanguage english(i18n::Language::english); material_cache::Lifetime material;
    const auto begun = Clock::now();
    int completed = 0, failed = 0, next = start, chunk = 0;
    bool budgetStopped = false;
    juce::String csv("ordinal,tuple,choices,status,paint_cpu_ms,paint_wall_ms,arrays_sha256,pixel_sha256,immediate_hover_checked\n");
    std::vector<double> paintTimes;
    const auto flush = [&] {
        const auto file = output.getChildFile("chunk-" + juce::String(chunk++) + ".csv");
        require(!file.exists() && hypha::storage::writeText(file, csv), "CSV write failed");
        csv = "ordinal,tuple,choices,status,paint_cpu_ms,paint_wall_ms,arrays_sha256,pixel_sha256,immediate_hover_checked\n";
    };
    for (int ordinal = start; ordinal < start + count; ++ordinal) {
        // One CPU tuple is three32-second runs. Never start it without room in the caller budget.
        const double remaining = limitSeconds - ms(Clock::now() - begun) / 1000;
        if (remaining < (mode == "cpu" ? 100.0 : 0.05)) { budgetStopped = true; break; }
        const int id = choiceMode ? ids[static_cast<size_t>(ordinal)] : ordinal;
        const auto tuple = decode(id); ScreenResult result; bool ok = true;
        try {
            roundtrip(id, tuple);
            if (screenMode) { result = screen(tuple, width, input); paintTimes.push_back(result.cpuMs); }
            if (mode == "cpu") {
                for (int repetition = 1; repetition <= 3; ++repetition) {
                    auto row = freq_options::cpuRun(width, rate, repetition, source, tuple.values, tuple.average);
                    set(row, "tuple", id); set(row, "choices", choices(tuple));
                    saveJson(output.getChildFile("cpu-" + juce::String(id) + "-" + juce::String(repetition) + ".json"), row);
                    require(static_cast<bool>(row.getProperty("valid", false)), "invalid paced run");
                }
            }
        } catch (const std::exception& error) {
            ok = false; ++failed;
            auto failure = object(); set(failure, "tuple", id); set(failure, "what", juce::String(error.what()));
            saveJson(output.getChildFile("failure-" + juce::String(id) + ".json"), failure);
        }
        csv += juce::String(ordinal) + "," + juce::String(id) + "," + choices(tuple)
            + (ok ? ",PASS," : ",FAIL,") + juce::String(result.cpuMs, 6) + "," + juce::String(result.wallMs, 6)
            + "," + result.arrays + "," + result.pixels + (result.immediate ? ",true\n" : ",false\n");
        ++completed; next = ordinal + 1;
        if (completed % 256 == 0) flush();
    }
    if (completed % 256 != 0 || completed == 0) flush();
    auto summary = object(); set(summary, "schema", "spectrum-settings-qa-v1");
    set(summary, "source", source); set(summary, "fixture", input.sha256); set(summary, "mode", mode);
    set(summary, "width", width); set(summary, "fps", rate); set(summary, "population", total);
    set(summary, "start", start); set(summary, "requested", count); set(summary, "completed", completed);
    set(summary, "next", next); set(summary, "failed", failed); set(summary, "budget_stopped", budgetStopped);
    set(summary, "range_complete", completed == count); set(summary, "range_pass", failed == 0 && completed == count);
    set(summary, "population_kind", choiceMode ? "single-choice-29-not-cartesian" : "cartesian-184320");
    const bool completePopulation = start == 0 && count == total && completed == total && failed == 0;
    set(summary, "whole_population_pass", completePopulation);
    set(summary, "all_cartesian_pass", !choiceMode && completePopulation);
    set(summary, "all_single_choices_pass", choiceMode && completePopulation);
    set(summary, "wall_seconds", ms(Clock::now() - begun) / 1000); set(summary, "paint_cpu_ms", distribution(paintTimes));
    set(summary, "scope", "Preference serialization or four-frame full/LR/delta virtual paint; CPU mode is29 single-choice tuples, not all tuples/hosts");
    saveJson(output.getChildFile("summary.json"), summary);
    std::cout << "SETTINGS_QA " << mode << " start=" << start << " completed=" << completed
              << " next=" << next << " failed=" << failed << " budget_stop=" << budgetStopped << '\n';
    return failed ? 2 : (completed == count ? 0 : 3);
}
}
