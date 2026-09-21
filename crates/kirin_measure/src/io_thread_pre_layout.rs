//! PRE が pre.json に名乗る測定配置（B-976 / Gate A1）。
//!
//! **読めることと、比べてよいことは別である。** ここは前者（transport）だけを扱う。

/// `"layout":{...}` 断片。`None` なら空文字。
///
/// **`v` は 2 のまま。** 旧 POST は未知フィールドを無視して従来どおり読む（transport 互換）。
/// ここを `v=3` にすると旧 POST が新 PRE を**一切読めず、ペアリング自体が成立しない**。
/// pre.json は PRE/POST ペアリングの一次経路なので、その失敗の重さは釣り合わない。
///
/// **読めることと、比べてよいことは別である。** 比較可否は POST 側の
/// `compute_delta_for_pre_file` が判定する（B-976 / Gate A1）。
pub(super) fn layout_fragment(layout: Option<crate::channel_layout::ChannelLayout>) -> String {
    let Some(layout) = layout else {
        return String::new();
    };
    match serde_json::to_string(&crate::plugin_data::MeasurementLayout::new(layout)) {
        Ok(json) => format!(r#","layout":{json}"#),
        Err(_) => String::new(),
    }
}
