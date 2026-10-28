//! Construction of measured observations and timeline-only markers.
use super::*;

impl RecordTraceSample {
    pub fn measured(
        generation: u64,
        t_ms: u64,
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
        result: MeasureResult,
        include_psb: bool,
    ) -> Self {
        Self {
            kind: RecordTraceKind::Measured,
            generation,
            t_ms,
            t_frames_48k,
            t_native_frames,
            position_samples: None,
            raw_host_position_samples: None,
            capture_epoch: None,
            clock_source: CaptureClockSource::Unknown,
            presentation_latency: PresentationLatencySamples::default(),
            result,
            include_psb,
        }
    }

    pub fn measured_observed(
        generation: u64,
        t_ms: u64,
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
        result: MeasureResult,
        observed_silence_floor: bool,
        include_psb: bool,
    ) -> Self {
        // Only explicit floor provenance can fill a missing loudness measurement. An unready
        // window also has None, but must remain missing. Never repair non-finite core input.
        let measured_floor = result.momentary_silence_floor
            && result.lufs_m.is_none()
            && [result.true_peak, result.crest]
                .into_iter()
                .flatten()
                .all(f64::is_finite);
        let mut result = result;
        if observed_silence_floor && !measure_has_core_metrics(&result) {
            result = with_trace_silence_core(result);
        } else if measured_floor {
            // Preserve measured finite values; only absent core fields need floor placeholders.
            result.lufs_m.get_or_insert(TRACE_SILENCE_LUFS);
            result.true_peak.get_or_insert(TRACE_SILENCE_TRUE_PEAK_DBTP);
            result.crest.get_or_insert(TRACE_SILENCE_CREST_DB);
        }
        Self::measured(
            generation,
            t_ms,
            t_frames_48k,
            t_native_frames,
            result,
            include_psb,
        )
    }

    pub fn missing_marker(
        generation: u64,
        t_ms: u64,
        t_frames_48k: u64,
        t_native_frames: Option<u64>,
    ) -> Self {
        Self {
            kind: RecordTraceKind::Missing,
            generation,
            t_ms,
            t_frames_48k,
            t_native_frames,
            position_samples: None,
            raw_host_position_samples: None,
            capture_epoch: None,
            clock_source: CaptureClockSource::Unknown,
            presentation_latency: PresentationLatencySamples::default(),
            result: MeasureResult::default(),
            include_psb: false,
        }
    }

    pub fn has_measured_core(&self) -> bool {
        self.kind == RecordTraceKind::Measured && measure_has_core_metrics(&self.result)
    }

    pub fn with_position_samples(mut self, position_samples: Option<i64>) -> Self {
        self.position_samples = position_samples;
        self
    }

    pub fn with_clock_point(
        mut self,
        point: Option<crate::record_take::CaptureClockPoint>,
    ) -> Self {
        if let Some(point) = point {
            self.position_samples = Some(point.position_samples);
            self.raw_host_position_samples = Some(point.raw_host_position_samples);
            self.capture_epoch = Some(point.epoch);
            self.clock_source = point.source;
            self.presentation_latency = point.presentation_latency;
        }
        self
    }

    pub fn with_clock_source(mut self, clock_source: CaptureClockSource) -> Self {
        self.clock_source = clock_source;
        self
    }

    pub fn with_presentation_latency(
        mut self,
        presentation_latency: PresentationLatencySamples,
    ) -> Self {
        self.presentation_latency = presentation_latency;
        self
    }
}
