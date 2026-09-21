//! Record delta styling only; values and frozen-value selection stay in the editor.

use hypha_gui::{COL_MUTED, COL_NORMAL};
use kirin_measure::DeltaMode;
use nih_plug_egui::egui::Color32;

pub(super) fn delta_color(mode: &DeltaMode, muted: bool) -> Color32 {
    if muted {
        COL_MUTED
    } else {
        match mode {
            DeltaMode::Active => COL_NORMAL,
            DeltaMode::Stale => COL_MUTED,
            DeltaMode::Bypassed => COL_MUTED,
            DeltaMode::PreInactive => COL_MUTED,
            DeltaMode::NoPre => COL_MUTED,
            // A refused or unknown layout comparison stays muted in the legacy editor.
            DeltaMode::LayoutMismatch | DeltaMode::LayoutUnknown => COL_MUTED,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_an_active_unmuted_comparison_uses_normal_color() {
        let cases = [
            (DeltaMode::Active, COL_NORMAL),
            (DeltaMode::Stale, COL_MUTED),
            (DeltaMode::Bypassed, COL_MUTED),
            (DeltaMode::PreInactive, COL_MUTED),
            (DeltaMode::NoPre, COL_MUTED),
            (DeltaMode::LayoutMismatch, COL_MUTED),
            (DeltaMode::LayoutUnknown, COL_MUTED),
        ];
        for (mode, expected) in cases {
            assert_eq!(delta_color(&mode, false), expected);
            assert_eq!(delta_color(&mode, true), COL_MUTED);
        }
    }
}
