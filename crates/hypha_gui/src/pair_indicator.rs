//! Compact, redundant pair-state indicator used by PRE and POST.

use kirin_measure::PairStatus;
use nih_plug_egui::egui::{Color32, RichText, Ui};

use crate::{COL_FLORA, COL_LED_BLUE, COL_MUTED};

pub fn pair_indicator_text(status: PairStatus) -> &'static str {
    match status {
        PairStatus::Unpaired => "PAIR —",
        PairStatus::Waiting => "PAIR ◌",
        PairStatus::Paired => "PAIR ●",
    }
}

pub fn pair_indicator_color(status: PairStatus) -> Color32 {
    match status {
        PairStatus::Unpaired => COL_MUTED,
        PairStatus::Waiting => COL_FLORA,
        PairStatus::Paired => COL_LED_BLUE,
    }
}

pub fn draw_pair_indicator(ui: &mut Ui, status: PairStatus) {
    ui.label(
        RichText::new(pair_indicator_text(status))
            .size(11.0)
            .color(pair_indicator_color(status))
            .monospace(),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn text_and_colour_are_redundant_for_every_state() {
        assert_eq!(pair_indicator_text(PairStatus::Unpaired), "PAIR —");
        assert_eq!(pair_indicator_text(PairStatus::Waiting), "PAIR ◌");
        assert_eq!(pair_indicator_text(PairStatus::Paired), "PAIR ●");
        assert_ne!(
            pair_indicator_color(PairStatus::Unpaired),
            pair_indicator_color(PairStatus::Waiting)
        );
        assert_ne!(
            pair_indicator_color(PairStatus::Waiting),
            pair_indicator_color(PairStatus::Paired)
        );
    }
}
