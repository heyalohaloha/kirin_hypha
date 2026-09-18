//! Record が残す host clock と TRACE 診断の型。
//!
//! `plugin_data.rs` から分離した（B-960）。いずれも serde の平坦な値で、振る舞いを持たない。
//! `PluginDataFile` が optional field として抱え、`plugin_data` が re-export する。

use serde::{Deserialize, Serialize};

use super::Frame;

/// TRACE bake diagnostics. These fields separate a real measured silence frame
/// from a missing TRACE slot so downstream UI never has to infer absence from
/// `-100`.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct TraceDiagnostics {
    pub raw_trace_count: u64,
    pub expected_frame_count: u64,
    pub measured_frame_count: u64,
    pub missing_slots: u64,
    pub explicit_silence_frame_count: u64,
}

/// Producer-owned absolute sample clock used to bake every TRACE frame.
///
/// `origin_position_samples` is the absolute callback position corresponding to `t_ms = 0`.
/// Every measured slot must independently derive the same origin before this metadata is written.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct TraceClock {
    pub basis: String,
    pub origin_position_samples: i64,
    pub end_position_samples: i64,
    pub sample_rate: u32,
    pub sources: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
pub struct HostPresentationLatencyObservation {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub input_samples: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub output_samples: Option<u32>,
}

/// Record-only measurement candidate with the untouched host clock facts that formed it.
///
/// Unlike `trace_slot_positions`, this journal is not a public TRACE axis. It survives until the
/// dropped WAV is known so pair finalization can test host-specific presentation-clock models per
/// side and per latency epoch without touching metric values or retaining unbounded raw audio.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TraceClockObservation {
    pub frame: Frame,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub producer_position_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub raw_host_position_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub capture_epoch: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub clock_source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub presentation_latency_source: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub input_presentation_latency_samples: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub output_presentation_latency_samples: Option<u32>,
}

/// Unmodified host render range retained as diagnostics after TRACE is normalized to the output
/// presentation/WAV axis. It is never used by consumers to shift a curve.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub struct HostClockRange {
    pub start_position_samples: i64,
    pub end_position_samples: i64,
}

/// この Record がどの入力配置を、どの map で測ったかを、記録自身が語れるようにする。
///
/// **入力の事実と実際の測定を分けて持つ**（計画 §5.4）。チャンネル数から配置を推測しないのと
/// 同じ理由で、配置名から適用 map を推測しない。同じ `7.1.4` でも、重み付けの解釈が違えば
/// 別の測定である（決定 §4.1）。
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct MeasurementLayout {
    /// `LayoutId::as_str()`。`"stereo"` / `"5.1"` / `"7.1.4"`。
    pub layout: String,
    /// バッファ順の役割名（`ChannelRole::as_str()`）。**index ではなく役割で言う**（D-5）。
    pub channel_positions: Vec<String>,
    /// ebur128 へ実際に渡した map。`channel_positions` と同じ並び。LFE は `"unused"`。
    pub loudness_map: Vec<String>,
    /// map 規則の版（`MAPPING_REVISION`）。重み付けの解釈を変えたら上がる。
    pub mapping_revision: u32,
}

impl MeasurementLayout {
    /// 交渉済み layout から組み立てる。
    ///
    /// `measurement_epoch` はここに入れない。Record は既に `record_session_id` と
    /// `capture_generation_id` で 1 take を特定しており、区間識別子との関係付けは P-3 の設計である
    /// （棚卸し §8: 既存識別子をすべて新しい epoch へ置換するのではない）。
    pub fn new(layout: crate::channel_layout::ChannelLayout) -> Self {
        Self {
            layout: layout.id().as_str().to_string(),
            channel_positions: layout
                .roles()
                .iter()
                .map(|role| role.as_str().to_string())
                .collect(),
            loudness_map: layout
                .roles()
                .iter()
                .map(|role| role.loudness_channel_name().to_string())
                .collect(),
            mapping_revision: crate::channel_layout::MAPPING_REVISION,
        }
    }

    /// 記録された実チャンネル数。`channel_positions` の長さがその事実である。
    pub fn channel_count(&self) -> usize {
        self.channel_positions.len()
    }
}
