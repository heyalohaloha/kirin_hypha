//! What Spectrum / Sharpness / Attack are looking at (承認事項 3A / D-5)。
//!
//! **selector は layout 上の役割で指す。index では指さない。** index だと 7.1.4 → 5.1.4 の
//! layout 変更で「index 7 は有効なまま、指すチャンネルだけが変わる」。生の `u8` を比べる
//! `set_channel_mode` は差分を見つけられず、**別チャンネルの履歴が無言で連結する**
//! （契約表 §11.3.1）。役割で指せば、layout 変更は「役割が残る」か「役割が消える」かの
//! どちらかになり、どちらも検出できる。
//!
//! ABI コード 0 / 1 / 2 は既存の LR / MID / SIDE のまま。**振り直さない。**

use super::{ChannelLayout, ChannelRole, LayoutId};

/// 単一チャンネル view の ABI コード基点。`ROLE_BASE + ChannelRole::to_abi()`。
///
/// 3..15 は空けてある。導出 view を足すならそこで、役割の番号には触れない。
pub const SPECTRUM_VIEW_ROLE_BASE: u8 = 16;

/// 「観測対象が無い」ことを表す ABI 値。どの view とも重ならない。
/// 既定値ではなく、**解析経路がこの layout を測れない**という事実である。
pub const SPECTRUM_VIEW_NONE: u8 = 255;

/// 観測している対象。導出 view か、名前の付いた 1 チャンネルか。
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SpectrumView {
    /// L と R の power 平均。mono では そのチャンネル。既定値。
    Lr,
    /// 波形領域の `0.5·(L + R)`。**M/S 分解そのものなので stereo 専用。**
    Mid,
    /// 波形領域の `0.5·(L − R)`。同上。
    Side,
    /// 名前で指した 1 チャンネル。`C` を選べば C の観測である。
    Channel(ChannelRole),
}

impl SpectrumView {
    pub fn to_abi(self) -> u8 {
        match self {
            Self::Lr => 0,
            Self::Mid => 1,
            Self::Side => 2,
            Self::Channel(role) => SPECTRUM_VIEW_ROLE_BASE + role.to_abi(),
        }
    }

    /// コードが指す view。未知のコードは `None`。**近い view へ丸めない。**
    pub fn from_abi(code: u8) -> Option<Self> {
        match code {
            0 => Some(Self::Lr),
            1 => Some(Self::Mid),
            2 => Some(Self::Side),
            SPECTRUM_VIEW_NONE => None,
            _ if code >= SPECTRUM_VIEW_ROLE_BASE => {
                ChannelRole::from_abi(code - SPECTRUM_VIEW_ROLE_BASE).map(Self::Channel)
            }
            _ => None,
        }
    }

    /// 記録・表示のための短い名前。
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Lr => "LR",
            Self::Mid => "MID",
            Self::Side => "SIDE",
            Self::Channel(role) => role.as_str(),
        }
    }

    /// この layout でこの view を選べるか。
    ///
    /// **mono / stereo の現行挙動をそのまま写す。** mono は LR と MID を受け、SIDE を拒否する
    /// （`spectrum_runtime.rs` の旧 guard と同じ。mono の MID は `0.5·(L+R)` の右が無いので
    /// そのチャンネルである）。決めるのはサラウンドの場合だけで、そこでは導出 view を出さない。
    ///
    /// 5.1 の L と R から `0.5·(L ± R)` は作れるが、それは前方ペアの M/S であって作品の M/S では
    /// ない。**同じラベルのまま意味を変えない**（D-6 / 契約表 §6）。
    pub fn is_available_in(self, layout: ChannelLayout) -> bool {
        let stereo_family = matches!(layout.id(), LayoutId::Mono | LayoutId::Stereo);
        match self {
            Self::Lr | Self::Mid => stereo_family,
            Self::Side => layout.id() == LayoutId::Stereo,
            Self::Channel(role) => layout.index_of(role).is_some(),
        }
    }

    /// 解析経路がこの view を実際に測れるか。
    ///
    /// **P-3 時点では導出 view（LR / MID / SIDE）だけである。** 単一チャンネル view は値空間と
    /// 検証は揃ったが、`update_power` / `analyze_mono` がまだ役割で入力を選ばない（P-4）。
    /// 選べてしまうと `view()` が `Rss` と答えながら frame は LR を運ぶ。
    /// **値が出ているのに意味が違う状態を作らない**（D-13）。P-4 でこの関数は消える。
    pub fn is_analysable(self) -> bool {
        matches!(self, Self::Lr | Self::Mid | Self::Side)
    }

    /// この layout で選べる view の全体。導出 view が先、その後にチャンネルがバッファ順で並ぶ。
    pub fn available_in(layout: ChannelLayout) -> Vec<Self> {
        let derived = [Self::Lr, Self::Mid, Self::Side]
            .into_iter()
            .filter(|view| view.is_available_in(layout));
        let channels = layout.roles().iter().copied().map(Self::Channel);
        derived.chain(channels).collect()
    }

    /// この layout の既定 view。
    ///
    /// LR を持つ layout では LR のまま（現行の既定を変えない）。持たない layout では
    /// **バッファ先頭の役割**にする。既定が無い状態を作らない。
    pub fn default_for(layout: ChannelLayout) -> Self {
        if Self::Lr.is_available_in(layout) {
            return Self::Lr;
        }
        Self::Channel(layout.roles()[0])
    }
}

#[cfg(test)]
#[path = "spectrum_view_tests.rs"]
mod tests;
