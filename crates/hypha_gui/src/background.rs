//! 背景テクスチャ — 300×200 菌糸 PNG（brightness 15%）。
//!
//! PNG は `assets/bg_mycelium.png` から `include_bytes!` で埋め込む。
//! decode は `BackgroundTexture` 1 個につき 1 回のみ（`ColorImage` を保持）。
//!
//! # ライフサイクル対応
//!
//! nih_plug_egui はエディタを閉じるたびに baseview ウィンドウを破棄し、再オープン時に
//! 新しい `egui::Context` を生成する（古い `TextureHandle` は新 Context では無効）。
//! 本構造体は `ctx_snapshot` に前回登録時の `Context` を保持し、
//! `PartialEq`（内部は `Arc::ptr_eq`）で変化を検出して `ctx.load_texture()`
//! で再登録する。

use nih_plug_egui::egui::{self, Color32, ColorImage, Context, TextureHandle, TextureOptions};

const BG_BYTES: &[u8] = include_bytes!("../assets/bg_mycelium.png");

/// 背景テクスチャのロード状態。エディタ state に 1 つ置く。
pub struct BackgroundTexture {
    /// decode 済み ColorImage（struct lifetime でキャッシュ）
    image: Option<ColorImage>,
    /// 現 Context に登録済みの TextureHandle
    handle: Option<TextureHandle>,
    /// handle を登録した時点の Context（変化検出用）
    ctx_snapshot: Option<Context>,
    /// decode 失敗で再試行を止めるフラグ
    failed: bool,
}

impl Default for BackgroundTexture {
    fn default() -> Self {
        Self::new()
    }
}

impl BackgroundTexture {
    pub fn new() -> Self {
        Self {
            image: None,
            handle: None,
            ctx_snapshot: None,
            failed: false,
        }
    }

    /// egui ペインタで背景を塗る。decode 失敗時は何もしない（呼び出し側の panel_fill が見える）。
    pub fn paint(&mut self, ctx: &Context, ui: &mut egui::Ui) {
        if self.failed {
            return;
        }
        // 1. decode（struct lifetime で 1 回のみ）
        if self.image.is_none() {
            match decode_png(BG_BYTES) {
                Ok(img) => self.image = Some(img),
                Err(_) => {
                    self.failed = true;
                    return;
                }
            }
        }
        // 2. Context 変化検出 → handle 再登録
        let need_reload = self.handle.is_none() || self.ctx_snapshot.as_ref() != Some(ctx);
        if need_reload {
            if let Some(img) = self.image.as_ref() {
                self.handle = Some(ctx.load_texture(
                    "hypha_bg_mycelium",
                    img.clone(),
                    TextureOptions::LINEAR,
                ));
                self.ctx_snapshot = Some(ctx.clone());
                // 初期ロード直後の GPU アップロード遅延対策として追加 repaint を要求
                ctx.request_repaint();
            }
        }
        // 3. 描画（`ctx.screen_rect()` で常にウィンドウ全体を取得。
        //   `ui.max_rect()` は CentralPanel 初回 frame のレイアウト計算が
        //   確定する前に呼ばれると縮退したサイズを返す場合がある）
        let Some(handle) = self.handle.as_ref() else { return };
        let rect = ctx.screen_rect();
        ui.painter().image(
            handle.id(),
            rect,
            egui::Rect::from_min_max(egui::pos2(0.0, 0.0), egui::pos2(1.0, 1.0)),
            Color32::WHITE,
        );
    }
}

/// PNG バイト列を egui の `ColorImage` に decode する。
fn decode_png(bytes: &[u8]) -> Result<ColorImage, String> {
    let decoder = png::Decoder::new(bytes);
    let mut reader = decoder.read_info().map_err(|e| e.to_string())?;
    let mut buf = vec![0; reader.output_buffer_size()];
    let info = reader.next_frame(&mut buf).map_err(|e| e.to_string())?;
    let w = info.width as usize;
    let h = info.height as usize;

    let raw = &buf[..info.buffer_size()];
    let rgba: Vec<u8> = match info.color_type {
        png::ColorType::Rgba => raw.to_vec(),
        png::ColorType::Rgb => raw
            .chunks_exact(3)
            .flat_map(|p| [p[0], p[1], p[2], 255])
            .collect(),
        png::ColorType::GrayscaleAlpha => raw
            .chunks_exact(2)
            .flat_map(|p| [p[0], p[0], p[0], p[1]])
            .collect(),
        png::ColorType::Grayscale => raw.iter().flat_map(|&v| [v, v, v, 255]).collect(),
        png::ColorType::Indexed => return Err("indexed PNG not supported".into()),
    };

    if rgba.len() != w * h * 4 {
        return Err(format!(
            "size mismatch: got {} bytes, expected {}",
            rgba.len(),
            w * h * 4
        ));
    }
    Ok(ColorImage::from_rgba_unmultiplied([w, h], &rgba))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn embedded_png_decodes_to_300x200() {
        let img = decode_png(BG_BYTES).expect("decode failed");
        assert_eq!(img.size, [300, 200]);
    }
}
