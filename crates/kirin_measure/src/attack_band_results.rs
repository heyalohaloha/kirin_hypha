//! The band results one ATTACK runtime publishes: its own hits in the chosen band and, on POST,
//! the hits measured at PRE onsets. The worker is their only writer and measures each hit once
//! per band; readers take the published `Arc` and never wait on a measurement. They are kept out
//! of `AttackHistory`, so choosing a band adds nothing to the history every poll copies.

use super::state::AttackEvent;
use crate::attack_perception::band::{
    AttackBand, AttackBandMeasure, BandSpanEnd, ATTACK_BAND_HISTORY_CAPACITY,
};

/// One band of one hit: measured, or its audio was not kept.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackBandDetail {
    /// The hit (own), or the PRE onset POST was measured at (anchored).
    pub event: AttackEvent,
    pub band: AttackBand,
    /// The tail this result was asked for; for an anchored result the PRE side's. The measure
    /// may end earlier when its run's audio ended first.
    pub span_end_sample: i64,
    /// `None`: the audio of this hit was not kept. It came before the band was chosen, or its
    /// run's audio ended before its peak could be found.
    pub measure: Option<AttackBandMeasure>,
}

impl AttackBandDetail {
    pub fn has_valid_layout(&self) -> bool {
        self.event.has_valid_layout()
            && self.span_end_sample > self.event.event_sample
            && self.measure.is_none_or(|measure| {
                measure.has_valid_layout()
                    && measure.band == self.band
                    && measure.event_sample == self.event.event_sample
                    && measure.sample_rate == self.event.sample_rate
                    && measure.channels == self.event.channels
                    && measure.span_end_sample <= self.span_end_sample
            })
    }
}

/// A PRE onset POST is asked to measure, over PRE's tail and for PRE's reason it ends there.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BandAnchor {
    pub event: AttackEvent,
    pub span_end_sample: i64,
    pub span_end: BandSpanEnd,
}

/// Whether PRE's side of a band comparison is there, as POST sees it.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum AttackPreBand {
    /// No band is chosen, or no PRE is paired.
    #[default]
    Off,
    /// PRE measures the chosen band.
    Same,
    /// PRE has not declared the chosen band yet.
    Waiting,
    /// PRE's snapshots carry no band at all long after it was asked: a PRE that predates bands.
    Predates,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AttackBandResults {
    pub band: Option<AttackBand>,
    /// The run these hits belong to; 0 before any.
    pub generation: u64,
    own: Vec<AttackBandDetail>,
    anchored: Vec<AttackBandDetail>,
}

impl AttackBandResults {
    pub fn new(band: Option<AttackBand>, generation: u64) -> Self {
        Self {
            band,
            generation,
            own: Vec::new(),
            anchored: Vec::new(),
        }
    }

    /// This side's own hits, oldest first.
    pub fn own(&self) -> &[AttackBandDetail] {
        &self.own
    }

    /// POST measured at PRE onsets, oldest first.
    pub fn anchored(&self) -> &[AttackBandDetail] {
        &self.anchored
    }

    pub fn own_at(&self, onset: i64) -> Option<&AttackBandDetail> {
        find(&self.own, onset)
    }

    /// The anchored result at `onset` for exactly this tail; one for another tail is stale.
    pub fn anchored_at(&self, onset: i64, span_end: i64) -> Option<&AttackBandDetail> {
        find(&self.anchored, onset).filter(|detail| detail.span_end_sample == span_end)
    }

    /// Keeps a result, replacing the one at the same onset. False, and nothing kept, for a
    /// detail of another band or run or with an invalid layout. The runtime's published results
    /// are written by its worker alone; a caller builds its own set with these (a decoder, a test).
    pub fn put_own(&mut self, detail: AttackBandDetail) -> bool {
        self.accepts(&detail) && put(&mut self.own, detail)
    }

    pub fn put_anchored(&mut self, detail: AttackBandDetail) -> bool {
        self.accepts(&detail) && put(&mut self.anchored, detail)
    }

    fn accepts(&self, detail: &AttackBandDetail) -> bool {
        self.band == Some(detail.band)
            && detail.event.generation == self.generation
            && detail.has_valid_layout()
    }
}

fn find(list: &[AttackBandDetail], onset: i64) -> Option<&AttackBandDetail> {
    list.binary_search_by_key(&onset, |detail| detail.event.event_sample)
        .ok()
        .map(|index| &list[index])
}

/// Keeps `list` sorted by onset and within the event history's bound, dropping the oldest.
fn put(list: &mut Vec<AttackBandDetail>, detail: AttackBandDetail) -> bool {
    match list.binary_search_by_key(&detail.event.event_sample, |current| {
        current.event.event_sample
    }) {
        Ok(index) => list[index] = detail,
        Err(index) => {
            list.insert(index, detail);
            if list.len() > ATTACK_BAND_HISTORY_CAPACITY {
                list.remove(0);
            }
        }
    }
    true
}
