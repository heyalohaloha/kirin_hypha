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
        let Some(handle) = self.handle.as_ref() else {
            return;
        };
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
            .as_chunks::<3>()
            .0
            .iter()
            .flat_map(|p| [p[0], p[1], p[2], 255])
            .collect(),
        png::ColorType::GrayscaleAlpha => raw
            .as_chunks::<2>()
            .0
            .iter()
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

    // ── U-8 / G-60-03: CE 2226 PNG アセット描画系の安全網 ────────────────

    /// 埋込 PNG の RGBA ピクセル数が 300×200×4 になること。
    /// `Vec<Color32>` ではなく size + alpha/RGB ピクセル列の妥当性を直接確認。
    #[test]
    fn embedded_png_pixels_are_well_formed() {
        let img = decode_png(BG_BYTES).expect("decode failed");
        assert_eq!(img.pixels.len(), 300 * 200, "300x200 pixel count");
        // 暗い菌糸テクスチャ → 各ピクセルのアルファは不透明に近いこと（>=128）
        // かつ RGB は低輝度側に寄っている（brightness ~15% = RGB <= 96 目安）
        let mut bright = 0usize;
        for p in &img.pixels {
            if p.a() < 128 {
                panic!("alpha too low at pixel: a={}", p.a());
            }
            if p.r() > 96 || p.g() > 96 || p.b() > 96 {
                bright += 1;
            }
        }
        // 全ピクセルが暗いわけではない（ハイライトピクセルが混ざるのは許容）。
        // ただし半数超が明るいなら assets の取り違えが疑われる → 明示失敗。
        assert!(
            bright < img.pixels.len() / 2,
            "too many bright pixels: {}/{} — assets may be wrong",
            bright,
            img.pixels.len()
        );
    }

    /// 不正 PNG（マジック不正）→ Err を返し panic しない（R-28 沈黙原則の基盤）。
    #[test]
    fn decode_rejects_invalid_bytes() {
        let bogus = b"not a PNG file";
        let res = decode_png(bogus);
        assert!(res.is_err(), "bogus bytes must error, got: {:?}", res);
    }

    /// 空バイト列 → Err（panic しない）。
    #[test]
    fn decode_rejects_empty_bytes() {
        let res = decode_png(b"");
        assert!(res.is_err());
    }

    /// BackgroundTexture::new / Default は 失敗 = false で開始する。
    #[test]
    fn new_starts_unfailed() {
        let bg = BackgroundTexture::new();
        assert!(!bg.failed, "fresh texture must not be in failed state");
        let bg2: BackgroundTexture = Default::default();
        assert!(!bg2.failed);
    }
}
