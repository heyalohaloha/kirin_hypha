#pragma once
#include "SpectrumPacedBenchmarkSupport.h"
#include "../src/HyphaSpectrumPhaseProfile.h"
namespace hypha::tests::freq_paced {
#if defined(KIRIN_FREQ_PHASE_PROFILE)
inline constexpr bool phaseProfileBuild=true;
inline void startPhaseProfile() { check(freq_profile::reset(true),"profile reset during active scope"); }
inline void stopPhaseProfile() { check(freq_profile::stop(),"profile stack or depth error"); }
inline void exportPhaseProfile(juce::var& out, int rendered) {
    using namespace freq_profile;
    check(!state.enabled && state.depth==0 && !state.error,"profile must be stopped and balanced");
    constexpr std::array<const char*,23> names{{"paint_chrome","well_material_cache_and_draw","axes_and_labels",
        "legend_and_actions","paired_curves","curve_geometry_paths","pre_post_stroke_and_rich_fill",
        "delta_fill_geometry","delta_fill_stroke_glow","absolute_curves","history_total",
        "history_raster_render_callback","mid_side_curves","peak_tags","instrument_notes",
        "focus_trail","probe","guide_overlay","delta_gradient_body","delta_tip_ribbons",
        "delta_mark_and_zero_lines","delta_primary_strokes","delta_bucket_highlights"}};
    static_assert(names.size()==static_cast<std::size_t>(Phase::count));
    const auto captured=state.counters;
    const auto& chrome=captured[static_cast<std::size_t>(Phase::paint_chrome)];
    check(chrome.calls==static_cast<std::uint64_t>(rendered),"chrome paints must equal rendered frames");
    check(captured[static_cast<std::size_t>(Phase::paired_curves)].calls==chrome.calls,"paired curve paint count mismatch");
    const auto& delta=captured[static_cast<std::size_t>(Phase::delta_fill_stroke_glow)];
    std::uint64_t deltaChildren=0;
    for(auto phase:{Phase::delta_gradient_body,Phase::delta_tip_ribbons,Phase::delta_mark_and_zero_lines,
                    Phase::delta_primary_strokes,Phase::delta_bucket_highlights}) {
        const auto& child=captured[static_cast<std::size_t>(phase)];
        check(child.calls==delta.calls,"delta child count must equal parent count");
        deltaChildren+=child.inclusiveNs;
    }
    check(deltaChildren+delta.exclusiveNs==delta.inclusiveNs,"delta nested accounting mismatch");
    std::uint64_t exclusive=0; juce::Array<juce::var> phases;
    for(std::size_t i=0;i<names.size();++i) {
        const auto& c=captured[i]; check(c.exclusiveNs<=c.inclusiveNs,"exclusive time exceeds inclusive");
        exclusive+=c.exclusiveNs; auto phase=object(); set(phase,"name",names[i]);
        set(phase,"calls",static_cast<juce::int64>(c.calls));
        set(phase,"inclusive_wall_ns",static_cast<juce::int64>(c.inclusiveNs));
        set(phase,"exclusive_wall_ns",static_cast<juce::int64>(c.exclusiveNs)); phases.add(phase);
    }
    check(exclusive==chrome.inclusiveNs,"exclusive phase sum must equal chrome inclusive time");
    set(out,"profile_phases",phases); set(out,"profile_scope_clock","steady wall nanoseconds, NOT thread CPU");
    set(out,"profile_stack_balanced",true); set(out,"profile_counts_valid",true);
    // Separate diagnostic after measured interval. Never subtract this from measured phases or CPU.
    constexpr int samples=10000; check(reset(true),"empty-scope reset failed"); const auto begin=Clock::now();
    for(int i=0;i<samples;++i) { Scope empty(Phase::paint_chrome); }
    const auto end=Clock::now(); check(stop(),"empty-scope profile failed");
    auto overhead=object(); set(overhead,"samples",samples); set(overhead,"batch_wall_ms",ms(end-begin));
    set(overhead,"recorded_inclusive_ns",static_cast<juce::int64>(state.counters[0].inclusiveNs));
    set(overhead,"subtracted",false); set(out,"empty_scope_overhead",overhead);
    check(reset(false),"profile cleanup failed");
}
#else
inline constexpr bool phaseProfileBuild=false;
inline void startPhaseProfile() {}
inline void stopPhaseProfile() {}
inline void exportPhaseProfile(juce::var&,int) {}
#endif
}
