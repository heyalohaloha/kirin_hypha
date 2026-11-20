#pragma once
// Private diagnostic build only. No shipping class or inline product definition changes.
#if defined(KIRIN_FREQ_PHASE_PROFILE)
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
namespace hypha::freq_profile {
enum class Phase : std::size_t {
    paint_chrome, well_material_cache_and_draw, axes_and_labels, legend_and_actions,
    paired_curves, curve_geometry_paths, pre_post_stroke_and_rich_fill,
    delta_fill_geometry, delta_fill_stroke_glow, absolute_curves,
    history_total, history_raster_render_callback, mid_side_curves,
    peak_tags, instrument_notes, focus_trail, probe, guide_overlay,
    delta_gradient_body, delta_tip_ribbons, delta_mark_and_zero_lines,
    delta_primary_strokes, delta_bucket_highlights,
    well_glass_cache_total, well_frame_cache_total, well_cache_lookup, well_cache_build_and_keep, well_flat_clip_setup, well_cache_blit, well_direct_fallback, count
};
struct Counter { std::uint64_t calls=0, inclusiveNs=0, exclusiveNs=0; };
struct Entry { Phase phase; std::chrono::steady_clock::time_point start; std::uint64_t children=0; };
struct State {
    std::array<Counter, static_cast<std::size_t>(Phase::count)> counters{};
    std::array<Entry,16> stack{};
    std::size_t depth=0;
    std::array<std::array<std::uint64_t,4>,2> wellCacheEvents{}; // hit, first direct, build, bypass
    bool enabled=false, error=false;
};
inline thread_local State state;
// Call on the painting thread, only outside scopes. Stop collection before copying/exporting.
inline bool reset(bool enabled) {
    if(state.depth) { state.error=true; return false; }
    state=State{}; state.enabled=enabled; return true;
}
inline bool stop() {
    if(state.depth) { state.error=true; return false; }
    state.enabled=false; return !state.error;
}
inline bool inWell(int kind) {
    if(!state.enabled || (kind!=2 && kind!=3)) return false;
    for(std::size_t i=0;i<state.depth;++i)
        if(state.stack[i].phase==Phase::well_material_cache_and_draw) return true;
    return false;
}
inline void wellEvent(int kind, std::size_t event) {
    if(inWell(kind)) ++state.wellCacheEvents[static_cast<std::size_t>(kind-2)][event];
}
class Scope {
public:
    explicit Scope(Phase phase, bool selected=true) {
        if(!state.enabled || !selected) return;
        if(state.depth==state.stack.size()) { state.error=true; return; }
        slot=state.depth++; active=true;
        state.stack[slot]={phase,std::chrono::steady_clock::now(),0};
    }
    Scope(const Scope&)=delete; Scope& operator=(const Scope&)=delete;
    ~Scope() { finish(); }
    void finish() {
        if(!active) return;
        active=false;
        if(state.depth!=slot+1) { state.error=true; return; }
        const auto now=std::chrono::steady_clock::now();
        const auto entry=state.stack[slot];
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-entry.start).count();
        const auto ns=static_cast<std::uint64_t>(elapsed);
        auto& c=state.counters[static_cast<std::size_t>(entry.phase)];
        ++c.calls; c.inclusiveNs+=ns;
        if(ns<entry.children) state.error=true;
        else c.exclusiveNs+=ns-entry.children;
        --state.depth;
        if(state.depth) state.stack[state.depth-1].children+=ns;
    }
private:
    std::size_t slot=0; bool active=false;
};
}
#define KIRIN_WELL_TOTAL(name, kind) const bool name##Selected=::hypha::freq_profile::inWell(kind); ::hypha::freq_profile::Scope name((kind)==2 ? ::hypha::freq_profile::Phase::well_glass_cache_total : ::hypha::freq_profile::Phase::well_frame_cache_total,name##Selected)
#define KIRIN_WELL_SCOPE(name, phase) ::hypha::freq_profile::Scope name(::hypha::freq_profile::Phase::phase,profileWellSelected)
#define KIRIN_WELL_EVENT(kind, event) ::hypha::freq_profile::wellEvent(kind,event)
#define KIRIN_FREQ_SCOPE(name, phase) ::hypha::freq_profile::Scope name(::hypha::freq_profile::Phase::phase)
#define KIRIN_FREQ_FINISH(name) name.finish()
#define KIRIN_FREQ_CALL(phase, ...) do { KIRIN_FREQ_SCOPE(freqCall, phase); __VA_ARGS__; } while(false)
#else
#define KIRIN_WELL_TOTAL(name, kind)
#define KIRIN_WELL_SCOPE(name, phase)
#define KIRIN_WELL_EVENT(kind, event)
#define KIRIN_FREQ_SCOPE(name, phase)
#define KIRIN_FREQ_FINISH(name)
#define KIRIN_FREQ_CALL(phase, ...) __VA_ARGS__
#endif
