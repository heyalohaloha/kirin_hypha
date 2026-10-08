use crate::channel_layout::{ChannelLayout, LayoutId};
use crate::{MeasureEngine, SessionSummary};

pub(crate) struct RecordMeasureEngines {
    trace: Option<MeasureEngine>,
    summary: Option<MeasureEngine>,
}

impl RecordMeasureEngines {
    pub(crate) fn new(sample_rate: u32, layout: ChannelLayout) -> Result<Self, String> {
        if !matches!(layout.id(), LayoutId::Mono | LayoutId::Stereo) {
            return Ok(Self {
                trace: None,
                summary: None,
            });
        }
        let mut summary = MeasureEngine::new(sample_rate, layout)?;
        summary.enable_session_summary_cache()?;
        Ok(Self {
            trace: Some(MeasureEngine::new(sample_rate, layout)?),
            summary: Some(summary),
        })
    }

    pub(crate) fn supported(&self) -> bool {
        self.trace.is_some() && self.summary.is_some()
    }

    pub(crate) fn trace(&mut self) -> &mut MeasureEngine {
        self.trace
            .as_mut()
            .expect("Record engine exists only for mono/stereo")
    }

    pub(crate) fn summary(&mut self) -> &mut MeasureEngine {
        self.summary
            .as_mut()
            .expect("Record engine exists only for mono/stereo")
    }

    /// Exact current processed-prefix facts; I/LRA reuse unchanged canonical-energy revisions.
    /// The stop/drain path separately calls the original scalar `finalize()` before sealing.
    pub(crate) fn intermediate_summary(&self) -> SessionSummary {
        self.summary
            .as_ref()
            .expect("Record summary engine")
            .cached_session_summary()
    }

    pub(crate) fn parts(&mut self) -> (&mut MeasureEngine, &mut MeasureEngine) {
        let Self { trace, summary } = self;
        (
            trace.as_mut().expect("Record TRACE engine"),
            summary.as_mut().expect("Record summary engine"),
        )
    }

    pub(crate) fn reset(&mut self) {
        if let Some(engine) = self.trace.as_mut() {
            engine.reset();
        }
        if let Some(engine) = self.summary.as_mut() {
            engine.reset();
        }
    }
}

#[cfg(test)]
#[path = "record_summary_tests.rs"]
mod summary_tests;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn five_one_constructs_no_record_engines() {
        let engines =
            RecordMeasureEngines::new(48_000, ChannelLayout::by_id(LayoutId::Surround5_1)).unwrap();
        assert!(!engines.supported());
        assert!(engines.trace.is_none());
        assert!(engines.summary.is_none());
    }
}
