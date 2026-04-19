//! パレット — Kirin Hypha GUI で使用する色定数。
//!
//! CE 2226 菌糸の先端。暗い背景 + 絞られたアクセント。
//! flora_color (amber #d4a043) は T-6 サブ1 時点での暫定値。実機調整後に差し替え。

use nih_plug_egui::egui::Color32;

/// 背景色。Hypha GUI 全体のパネル塗りつぶし。
pub const BG: Color32 = Color32::from_rgb(0x0D, 0x0F, 0x1A);

/// 通常テキスト / 値の色（ほぼ白）。
pub const COL_NORMAL: Color32 = Color32::from_rgb(0xE0, 0xE0, 0xE0);

/// ラベル / 単位 / 沈黙値の色（淡い灰）。
pub const COL_MUTED: Color32 = Color32::from_rgb(0x60, 0x60, 0x60);

/// flora_color 暫定値（amber #d4a043）。
///
/// 菌糸と Kirin OS をつなぐ線色。実機調整後に差し替え可能。
pub const COL_FLORA: Color32 = Color32::from_rgb(0xD4, 0xA0, 0x43);

/// flora_color 輝度増強版（amber #ffe0a0）。
///
/// guardian_54-16 修正: True Peak が -1.0 dBTP を超えた時の「光が強まった」
/// 環境型表現。色相は COL_FLORA と同一（0度）、明度のみ増加。
/// 赤系・ネオン・純白の禁止（G-54-16）を避けるための代替表現。
pub const COL_FLORA_BRIGHT: Color32 = Color32::from_rgb(0xFF, 0xE0, 0xA0);

// ── LED 色（5状態 LED で使用） ──────────────────────────────────────────

/// Watch 青（呼吸アニメーションのベース色）。
pub const COL_LED_BLUE: Color32 = Color32::from_rgb(0x44, 0x88, 0xCC);

/// Record 緑（待機淡 / 計測中脈動のベース色）。
pub const COL_LED_GREEN: Color32 = Color32::from_rgb(0x4C, 0xC0, 0x7A);

/// エラー黄（Measure Thread 停止時）。
pub const COL_LED_YELLOW: Color32 = Color32::from_rgb(0xCC, 0xAA, 0x44);

/// Idle 灰（非Active 時の静的 LED）。
pub const COL_LED_GREY: Color32 = Color32::from_rgb(0x55, 0x55, 0x58);
