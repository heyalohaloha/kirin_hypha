#pragma once
// Test-only range runner. Counts refer to settings tuples, never all host/audio states.
#include "SpectrumSettingsCoverage.h"
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
// --freq-settings-qa MODE OUT SOURCE START COUNT WIDTH FPS MAX_SECONDS PREVIOUS
// Optional trailing: DURATION_SECONDS REPEATS LIST_JSON_OR_DASH. Schema v2 resumes only.
inline int main(int argc, char** argv) {
    require(argc == 11 || argc == 14, "settings QA expects mode/out/source/start/count/width/fps/max-seconds/previous");
    const juce::String mode(argv[2]), source(argv[4]);
    const bool cpuMode = mode == "cpu" || mode == "list-cpu";
    const bool choiceMode = mode == "cpu" || mode == "choice-roundtrip" || mode == "choice-screen";
    const bool pairMode = mode == "pairwise-roundtrip" || mode == "pairwise-screen";
    const bool listMode = mode == "list-cpu" || mode == "list-roundtrip" || mode == "list-screen";
    const bool indexed = choiceMode || pairMode || listMode;
    const bool screenMode = mode == "screen" || mode == "choice-screen" || mode == "pairwise-screen" || mode == "list-screen";
    require(indexed || mode == "roundtrip" || mode == "screen", "unknown settings QA mode");
    const int start = integer(argv[5]), count = integer(argv[6]), width = integer(argv[7]);
    const int rate = integer(argv[8]), limitSeconds = integer(argv[9]);
    const int duration = argc == 14 ? integer(argv[11]) : 30;
    const int repeats = argc == 14 ? integer(argv[12]) : 3;
    require(duration >= 1 && duration <= 30 && repeats >= 1 && repeats <= 3, "duration1..30/repeats1..3 required");
    require(!listMode || (argc == 14 && juce::String(argv[13]) != "-"), "list mode needs a tuple list JSON");
    require(listMode || argc == 11 || juce::String(argv[13]) == "-", "unexpected list argument");
    const auto ids = listMode ? listIds(juce::File(argv[13])) : pairMode ? pairwiseIds() : singleChoiceIds();
    const auto selectionHash = indexed ? idsHash(ids) : juce::String("cartesian-184320-v1");
    const int total = indexed ? static_cast<int>(ids.size()) : population;
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
        require(old.isObject() && old.getProperty("schema", {}) == juce::var("spectrum-settings-qa-v2")
            && old.getProperty("source", {}) == juce::var(source)
            && old.getProperty("fixture", {}) == juce::var(input.sha256)
            && old.getProperty("mode", {}) == juce::var(mode)
            && static_cast<int>(old.getProperty("width", 0)) == width
            && static_cast<int>(old.getProperty("fps", 0)) == rate
            && old.getProperty("selection_sha256", {}) == juce::var(selectionHash)
            && static_cast<int>(old.getProperty("duration_seconds", 0)) == duration
            && static_cast<int>(old.getProperty("repeats", 0)) == repeats
            && static_cast<int>(old.getProperty("next", -1)) == start,
            "resume provenance/range mismatch");
    } else require(start == 0, "nonzero start requires previous summary");
    require(output.createDirectory().wasOk(), "cannot create QA result directory");
    const i18n::ScopedLanguage english(i18n::Language::english); material_cache::Lifetime material;
    const auto begun = Clock::now();
    int completed = 0, failed = 0, next = start, chunk = 0;
    bool budgetStopped = false;
    juce::String csv("ordinal,tuple,choices,status,paint_cpu_ms,paint_wall_ms,arrays_sha256,pixel_sha256,immediate_hover_checked\n");
    std::vector<double> paintTimes; std::vector<int> passedIds;
    const auto flush = [&] {
        const auto file = output.getChildFile("chunk-" + juce::String(chunk++) + ".csv");
        require(!file.exists() && hypha::storage::writeText(file, csv), "CSV write failed");
        csv = "ordinal,tuple,choices,status,paint_cpu_ms,paint_wall_ms,arrays_sha256,pixel_sha256,immediate_hover_checked\n";
    };
    for (int ordinal = start; ordinal < start + count; ++ordinal) {
        // Reserve the configured paced runs plus margin before starting a tuple.
        const double remaining = limitSeconds - ms(Clock::now() - begun) / 1000;
        if (remaining < (cpuMode ? (duration + 2.0) * repeats + 4.0 : 0.05)) { budgetStopped = true; break; }
        const int id = indexed ? ids[static_cast<size_t>(ordinal)] : ordinal;
        const auto tuple = decode(id); ScreenResult result; bool ok = true;
        try {
            roundtrip(id, tuple);
            if (screenMode) { result = screen(tuple, width, input); paintTimes.push_back(result.cpuMs); }
            if (cpuMode) {
                for (int repetition = 1; repetition <= repeats; ++repetition) {
                    auto row = freq_options::cpuRun(width, rate, repetition, source, tuple.values, tuple.average, duration);
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
        if (ok) passedIds.push_back(id);
        ++completed; next = ordinal + 1;
        if (completed % 256 == 0) flush();
    }
    if (completed % 256 != 0 || completed == 0) flush();
    auto summary = object(); set(summary, "schema", "spectrum-settings-qa-v2");
    set(summary, "source", source); set(summary, "fixture", input.sha256); set(summary, "mode", mode);
    set(summary, "width", width); set(summary, "fps", rate); set(summary, "population", total);
    set(summary, "start", start); set(summary, "requested", count); set(summary, "completed", completed);
    set(summary, "next", next); set(summary, "failed", failed); set(summary, "budget_stopped", budgetStopped);
    set(summary, "range_complete", completed == count); set(summary, "range_pass", failed == 0 && completed == count);
    set(summary, "population_kind", listMode ? "explicit-list-not-cartesian" : pairMode ? "pairwise-not-cartesian" : choiceMode ? "single-choice-29-not-cartesian" : "cartesian-184320");
    set(summary, "selection_sha256", selectionHash);set(summary, "duration_seconds", duration);set(summary, "repeats", repeats);
    const int passedPairs = pairCoverage(passedIds);
    set(summary, "pair_denominator", 597);set(summary, "passed_pair_coverage", passedPairs);
    set(summary, "selection_pair_coverage", indexed ? pairCoverage(ids) : 597);
    set(summary, "all_pairs_verified_this_segment", passedPairs == 597);
    if (indexed) {juce::Array<juce::var> savedIds;for(int id:ids)savedIds.add(id);set(summary, "ordered_tuple_ids", savedIds);}
    const bool completePopulation = start == 0 && count == total && completed == total && failed == 0;
    set(summary, "whole_population_pass", completePopulation);
    set(summary, "all_cartesian_pass", !indexed && completePopulation);
    set(summary, "all_single_choices_pass", choiceMode && completePopulation);
    set(summary, "wall_seconds", ms(Clock::now() - begun) / 1000); set(summary, "paint_cpu_ms", distribution(paintTimes));
    set(summary, "scope", "Declared tuple selection only; full/LR/delta virtual paint or configured-duration paced CPU; no all-host/steady-state claim");
    saveJson(output.getChildFile("summary.json"), summary);
    std::cout << "SETTINGS_QA " << mode << " start=" << start << " completed=" << completed
              << " next=" << next << " failed=" << failed << " budget_stop=" << budgetStopped << '\n';
    return failed ? 2 : (completed == count ? 0 : 3);
}
}
