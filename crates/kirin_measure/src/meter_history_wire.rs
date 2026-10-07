//! Bounded, versioned TIME transport envelope. IO workers only.
use super::*;
#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
pub(super) struct WirePoint {
    pub(super) generation: u64,
    pub(super) run_id: u64,
    pub(super) observed_frames: u64,
    pub(super) endpoint_samples: i64,
    pub(super) source: u8,
    pub(super) lufs_m: Option<f64>,
    pub(super) lufs_s: Option<f64>,
    pub(super) true_peak: Option<f64>,
    pub(super) correlation: Option<f64>,
    /// Absent from a PRE that predates PSR in the history; POST then leaves PSR's Δ empty.
    #[serde(default)]
    pub(super) psr: Option<f64>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
pub(super) struct Publication {
    pub(super) schema: u8,
    pub(super) pre_instance_id: String,
    pub(super) watch_owner_id: String,
    pub(super) daw_session_id: String,
    pub(super) sample_rate: u32,
    /// PRE が実際に測っている配置。**引き算が成立するのは同じ map で測った 2 本だけである。**
    /// mono の PRE と stereo の POST は、同じ音を通しても loudness で 3.01 LU ずれる
    /// （mono は 1ch として測り +3.01 dB バイアスを入れない）。その差は連鎖が加えたものではない。
    pub(super) layout: MeasurementLayout,
    pub(super) clock_policy: u8,
    pub(super) points: Vec<WirePoint>,
    pub(super) content_windows: Vec<ContentWirePoint>,
    #[serde(default)]
    pub(super) time: Option<TimePublication>,
}

#[derive(Deserialize)]
pub(super) struct PreIdentity {
    pub(super) instance_id: String,
    #[serde(default)]
    pub(super) daw_session_id: String,
    #[serde(default)]
    pub(super) watch_owner_id: String,
    #[serde(default)]
    pub(super) signal_state: String,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub(super) struct PairKey {
    pub(super) instance_id: String,
    pub(super) instance_dir: PathBuf,
    pub(super) owner_id: String,
    pub(super) daw_session_id: String,
    pub(super) post_binding: Option<PostBindingProvenance>,
}

impl WirePoint {
    pub(super) fn from_history(entry: MeterHistoryEntry) -> Option<Self> {
        Some(Self {
            generation: entry.generation,
            run_id: entry.run_id,
            observed_frames: entry.last_observed_frames,
            endpoint_samples: entry.last_timeline_endpoint_samples?,
            source: exact_source(entry.timeline_source)?,
            lufs_m: finite(entry.lufs_m.mean),
            lufs_s: finite(entry.lufs_s.mean),
            true_peak: finite(entry.true_peak.mean),
            correlation: finite(entry.correlation.mean),
            psr: finite(entry.psr.mean),
        })
    }

    pub(super) fn valid(&self) -> bool {
        matches!(self.source, 1 | 2)
            && [
                self.lufs_m,
                self.lufs_s,
                self.true_peak,
                self.correlation,
                self.psr,
            ]
            .into_iter()
            .flatten()
            .all(f64::is_finite)
    }
}

impl Publication {
    pub(super) fn valid_for(
        &self,
        identity: &PreIdentity,
        sample_rate: u32,
        layout: &MeasurementLayout,
    ) -> bool {
        self.schema == METER_HISTORY_EXCHANGE_SCHEMA
            && self.sample_rate == sample_rate
            && self.layout == *layout
            && self.pre_instance_id == identity.instance_id
            && self.watch_owner_id == identity.watch_owner_id
            && self.daw_session_id == identity.daw_session_id
            && self.points.len() <= METER_HISTORY_EXCHANGE_POINTS
            && self.points.iter().all(WirePoint::valid)
            && self.content_windows.len() <= METER_HISTORY_EXCHANGE_POINTS
            && self
                .content_windows
                .iter()
                .all(|point| point.valid(sample_rate))
    }
}

pub(super) fn read_pre_identity(path: &Path) -> Result<PreIdentity, String> {
    read_bounded_json(path)
}

pub(super) fn read_publication(instance_dir: &Path) -> Result<Publication, String> {
    read_bounded_json(&instance_dir.join(METER_HISTORY_EXCHANGE_FILE))
}

fn read_bounded_json<T: for<'de> Deserialize<'de>>(path: &Path) -> Result<T, String> {
    use std::io::Read;
    let metadata = fs::metadata(path).map_err(|error| error.to_string())?;
    if metadata.len() > MAX_EXCHANGE_BYTES {
        return Err("meter history exchange exceeds byte limit".to_string());
    }
    let mut bytes = Vec::new();
    fs::File::open(path)
        .map_err(|error| error.to_string())?
        .take(MAX_EXCHANGE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    if bytes.len() as u64 > MAX_EXCHANGE_BYTES {
        return Err("meter history exchange exceeds byte limit".to_string());
    }
    serde_json::from_slice(&bytes).map_err(|error| error.to_string())
}
