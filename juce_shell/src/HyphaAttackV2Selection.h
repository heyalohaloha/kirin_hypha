#pragma once
#include "HyphaAttackV2Model.h"
#include <algorithm>
#include <optional>

namespace hypha::attack_v2
{
struct LocatedEvent { KirinSnapshotEventKey key {}; float x = 0, halfWidth = 0; };
class Selection
{
public:
    std::optional<KirinSnapshotEventKey> selected;
    bool live = true, clustered = false, dragging = false;
    std::vector<LocatedEvent> frozen;
    std::size_t index = 0;
    float anchor = 0;
    void clear() { selected.reset(); live = true; clustered = dragging = false; frozen.clear(); }
    void latest (const std::vector<KirinSnapshotEventKey>& events)
    { if (live) selected = events.empty() ? std::nullopt : std::optional<KirinSnapshotEventKey> (events.back()); }
    void begin (const std::vector<LocatedEvent>& visible, float x)
    {
        if (clustered && std::abs (x - anchor) <= 8.0f) { cycle (true); dragging = true; return; }
        std::vector<LocatedEvent> near;
        for (const auto& e : visible) if (distance (e, x) <= 6.0f) near.push_back (e);
        if (near.empty())
        {
            if (visible.empty()) return;
            near.push_back (*std::min_element (visible.begin(), visible.end(), [x] (const auto& a, const auto& b) {
                return distance (a, x) < distance (b, x); }));
        }
        if (near.size() == 1 && selected && ! live && sameEvent (*selected, near[0].key)) { clear(); return; }
        live = false; clustered = near.size() > 1; dragging = true; anchor = near.front().x; index = 0;
        frozen = clustered ? std::move (near) : visible; // Normal drag also freezes its complete start set.
        selected = clustered ? frozen[0].key : near[0].key;
        if (! clustered) for (std::size_t i = 0; i < frozen.size(); ++i)
            if (sameEvent (frozen[i].key, *selected)) index = i;
    }
    void drag (float x)
    {
        if (! dragging || clustered || frozen.empty()) return;
        const auto at = std::min_element (frozen.begin(), frozen.end(), [x] (const auto& a, const auto& b) {
            return distance (a, x) < distance (b, x); });
        index = static_cast<std::size_t> (at - frozen.begin()); selected = at->key;
    }
    void cycle (bool right)
    {
        if (frozen.empty()) return;
        index = right ? (index + 1) % frozen.size() : (index + frozen.size() - 1) % frozen.size();
        selected = frozen[index].key; live = false;
    }
    void adjacent (const std::vector<KirinSnapshotEventKey>& current, bool right, bool home = false)
    {
        if (clustered) { if (home) { index = 0; selected = frozen[0].key; } else cycle (right); return; }
        if (current.empty()) return; // An empty window preserves a past LOCK.
        const auto found = selected ? std::find_if (current.begin(), current.end(), [&] (const auto& k) {
            return sameEvent (k, *selected); }) : current.end();
        if (home || found == current.end()) selected = right || home ? current.front() : current.back();
        else
        {
            const auto at = static_cast<std::size_t> (found - current.begin());
            selected = current[right ? (at + 1) % current.size() : (at + current.size() - 1) % current.size()];
        }
        live = false; dragging = false; frozen.clear();
    }
    void escape() { clustered = dragging = false; frozen.clear(); } // ESC leaves the selected LOCK intact.
    void reanchor (const std::vector<LocatedEvent>& locations)
    {
        if (! clustered || frozen.empty()) return;
        const auto at = std::find_if (locations.begin(), locations.end(), [&] (const auto& e) { return sameEvent (e.key, frozen[0].key); });
        if (at != locations.end()) anchor = at->x;
    }
private:
    static float distance (const LocatedEvent& e, float x) noexcept
    { return std::max (0.0f, std::abs (e.x - x) - e.halfWidth); }
};
}
