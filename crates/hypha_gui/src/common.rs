//! 共通ウィジェット / フォーマットヘルパー。
//!
//! PRE / POST エディタ間で重複していたコードを集約。

use crate::palette::{COL_FLORA, COL_FLORA_BRIGHT, COL_MUTED, COL_NORMAL};
use nih_plug_egui::egui::{self, Color32, RichText, Stroke, Vec2};

#[cfg(target_os = "macos")]
const MACOS_PROPORTIONAL_FONT_PATH: &str = "/System/Library/Fonts/SFNS.ttf";
#[cfg(target_os = "macos")]
const MACOS_MONOSPACE_FONT_PATH: &str = "/System/Library/Fonts/SFNSMono.ttf";

/// Install the same native typefaces used by the JUCE AU shell. This runs once on the GUI
/// thread; missing platform fonts silently retain the normal egui fallback fonts.
pub fn install_native_font_contract(ctx: &egui::Context) {
    #[cfg(target_os = "macos")]
    {
        use nih_plug_egui::egui::{FontData, FontDefinitions, FontFamily};
        use std::sync::Arc;

        let proportional = std::fs::read(MACOS_PROPORTIONAL_FONT_PATH).ok();
        let monospace = std::fs::read(MACOS_MONOSPACE_FONT_PATH).ok();
        if proportional.is_none() && monospace.is_none() {
            return;
        }

        let mut fonts = FontDefinitions::default();
        if let Some(bytes) = proportional {
            let name = "Hypha System".to_owned();
            fonts
                .font_data
                .insert(name.clone(), Arc::new(FontData::from_owned(bytes)));
            fonts
                .families
                .entry(FontFamily::Proportional)
                .or_default()
                .insert(0, name);
        }
        if let Some(bytes) = monospace {
            let name = "Hypha System Mono".to_owned();
            fonts
                .font_data
                .insert(name.clone(), Arc::new(FontData::from_owned(bytes)));
            fonts
                .families
                .entry(FontFamily::Monospace)
                .or_default()
                .insert(0, name);
        }
        ctx.set_fonts(fonts);
    }

    #[cfg(not(target_os = "macos"))]
    let _ = ctx;
}

/// label | value | unit の 1 行をグリッドに流し込む。
pub fn value_row(ui: &mut egui::Ui, label: &str, value: String, unit: &str, color: Color32) {
    ui.label(RichText::new(label).size(13.0).color(COL_MUTED));
    ui.label(RichText::new(value).size(16.0).color(color).monospace());
    ui.label(RichText::new(unit).size(11.0).color(COL_MUTED));
    ui.end_row();
}

/// 通常値 → 小数1桁。None → `---`。
pub fn fmt_val(v: Option<f64>) -> String {
    match v {
        Some(x) => format!("{:.1}", x),
        None => "---".to_string(),
    }
}

/// Δ値 → 符号付き小数1桁（+1.3 / -1.3 / `---`）。
pub fn fmt_delta(v: Option<f64>) -> String {
    match v {
        Some(x) if x >= 0.0 => format!("+{:.1}", x),
        Some(x) => format!("{:.1}", x),
        None => "---".to_string(),
    }
}

/// 通常値の色: 値あり=通常、None=muted。
pub fn val_color(v: Option<f64>) -> Color32 {
    if v.is_some() {
        COL_NORMAL
    } else {
        COL_MUTED
    }
}

/// True Peak の色: > -1.0 dBTP → 琥珀輝度増（G-54-16 修正）。
///
/// 赤系禁止（G-54-16）のため、色相は COL_FLORA と同一、明度のみ上げた
/// COL_FLORA_BRIGHT を使う。「光が強まった」環境型の表現。
/// 閾値は -1.0 dBTP（0 dBTP ではない）— 天井手前の警戒域を環境光で示す。
pub fn tp_color(tp: Option<f64>) -> Color32 {
    match tp {
        Some(x) if x > -1.0 => COL_FLORA_BRIGHT,
        Some(_) => COL_NORMAL,
        None => COL_MUTED,
    }
}

/// True Peak が警戒域（> -1.0 dBTP）かを判定する（Δ 表示時の色選択に使用）。
pub fn tp_over(tp: Option<f64>) -> bool {
    tp.map(|v| v > -1.0).unwrap_or(false)
}

/// flora_color の横線（菌糸の先端を示すアクセントライン）。
/// 幅は呼び出し元の ui.available_width() を使う。
pub fn flora_line(ui: &mut egui::Ui) {
    let w = ui.available_width();
    let (rect, _) = ui.allocate_exact_size(Vec2::new(w, 1.0), egui::Sense::hover());
    ui.painter()
        .hline(rect.x_range(), rect.center().y, Stroke::new(1.0, COL_FLORA));
}

/// PRE/POST ペアリング表示。
///
/// `pair_label` は空文字なら `"---"` を描画。長い文字列は省略しない（300px 幅に収まる想定）。
pub fn pairing_label(ui: &mut egui::Ui, pair_label: &str) {
    let text = if pair_label.is_empty() {
        "---"
    } else {
        pair_label
    };
    ui.label(RichText::new(text).size(11.0).color(COL_MUTED).monospace());
}
