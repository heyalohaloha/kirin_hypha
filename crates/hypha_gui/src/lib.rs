//! hypha_gui — PRE/POST 共通 GUI プリミティブ。
//!
//! サブ1: 5状態 LED / 背景テクスチャ / パレット / 共通ウィジェット。
//! nih_plug_egui (egui) に依存するが、Audio/Measure/IO Thread からは独立。

pub mod background;
pub mod common;
pub mod led;
pub mod palette;

pub use background::BackgroundTexture;
pub use common::{
    flora_line, fmt_delta, fmt_val, pairing_label, tp_color, tp_over, val_color, value_row,
};
pub use led::{derive_led_state, led_color, LedState};
pub use palette::{
    BG, COL_FLORA, COL_FLORA_BRIGHT, COL_LED_BLUE, COL_LED_GREEN, COL_LED_GREY, COL_LED_YELLOW,
    COL_MUTED, COL_NORMAL,
};
