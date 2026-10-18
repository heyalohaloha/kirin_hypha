//! The passes one Keep heard.
//!
//! A Keep's TRACE and WAV take stay the first continuous pass. A realtime LOOP wrap, a seek or a
//! restart after a stop begins a later pass: its audio reaches the live meters and the Keep
//! summary, never the TRACE. The record lists every pass with its host position range and frame
//! count, so the length of a long Keep is explained by the frames it accepted. A pass that is
//! not in the TRACE names why. A LOOP wrap is a normal pass; it never degrades the record.
//!
//! Measure Thread notes every Record-lane chunk; IO Thread reads the passes when the Record
//! closes; the editor reads one flag. The Audio Thread never touches this journal, so its lock
//! never reaches the atomics-only producer path of the take tracker that holds it.

use super::RecordTakeTracker;
use crate::capture_clock::{AuxiliaryClockSamples, AuxiliaryClockSource};
use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

/// The host's continuous clock ran on and the position returned as the host's own LOOP range
/// plays: a LOOP wrap.
pub const KEEP_PASS_LOOP_WRAP: &str = "loop_wrap";
/// The host's continuous clock ran on and the position moved without the LOOP naming it: a skip
/// ahead or a seek back.
pub const KEEP_PASS_POSITION_JUMP: &str = "position_jump";
/// The host's continuous clock did not run on (a stop and a new start) or was not reported.
pub const KEEP_PASS_TRANSPORT_RESTART: &str = "transport_restart";
/// The pass after an offline take (the host's position jump after a bounce): live display only.
pub const KEEP_PASS_AFTER_OFFLINE_TAKE: &str = "after_offline_take";
/// One entry after the full list: every further pass's frames, without host ranges.
pub const KEEP_PASS_MORE_PASSES: &str = "more_passes";

/// Enough for a two-hour Keep around a two-second loop; the record stays bounded beyond it.
const KEEP_PASS_LIMIT: usize = 4_096;

/// One pass of a Keep as written to the record (schema 1.3, additive).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct KeepPass {
    /// Untouched host callback position of the pass's first and one-past-last frame.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host_start_position_samples: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host_end_position_samples: Option<i64>,
    /// Native frames the Measure Thread accepted in this pass.
    pub frames: u64,
    /// Of those, the frames that reached the Keep summary (I, LRA, Max TP, PLR).
    pub summary_frames: u64,
    pub in_trace: bool,
    /// Why the pass is not in the TRACE; absent for the TRACE pass.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace_excluded_reason: Option<String>,
}

/// One consumed Record-lane chunk, as the Measure Thread routed it.
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct KeepChunk {
    pub generation: u64,
    pub raw_host_start: Option<i64>,
    pub auxiliary: AuxiliaryClockSamples,
    pub frames: u64,
    pub in_trace: bool,
    pub in_summary: bool,
    pub after_offline_take: bool,
    /// The chunk begins with the host's own LOOP wrap (the host's loop range named the return).
    pub host_loop_wrap: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct PassEdge {
    raw_end: Option<i64>,
    aux_end: Option<(AuxiliaryClockSource, i64)>,
    in_trace: bool,
    after_offline_take: bool,
}

#[derive(Debug, Default)]
struct Passes {
    generation: u64,
    passes: Vec<KeepPass>,
    edge: Option<PassEdge>,
}

/// The current Record generation's passes.
#[derive(Debug, Default)]
pub(crate) struct KeepPassJournal {
    passes: Mutex<Passes>,
    /// The Record generation whose TRACE already ended at its first pass.
    later_pass_generation: AtomicU64,
}

fn aux_point(aux: AuxiliaryClockSamples) -> Option<(AuxiliaryClockSource, i64)> {
    (aux.source != AuxiliaryClockSource::Unknown)
        .then_some(aux.samples)
        .flatten()
        .map(|samples| (aux.source, samples))
}

fn later_pass_reason(edge: Option<PassEdge>, chunk: &KeepChunk) -> &'static str {
    if chunk.after_offline_take {
        return KEEP_PASS_AFTER_OFFLINE_TAKE;
    }
    let Some(edge) = edge else {
        return KEEP_PASS_TRANSPORT_RESTART;
    };
    let continuous = edge.aux_end.is_some() && edge.aux_end == aux_point(chunk.auxiliary);
    match (continuous, edge.raw_end.zip(chunk.raw_host_start)) {
        (true, Some((end, start))) if start < end && chunk.host_loop_wrap => KEEP_PASS_LOOP_WRAP,
        (true, Some(_)) => KEEP_PASS_POSITION_JUMP,
        _ => KEEP_PASS_TRANSPORT_RESTART,
    }
}

impl KeepPassJournal {
    /// Measure Thread only. Returns true when this chunk began the generation's first pass
    /// outside the TRACE.
    pub(crate) fn note(&self, chunk: KeepChunk) -> bool {
        if chunk.frames == 0 || chunk.generation == 0 {
            return false;
        }
        let mut state = crate::sync_recovery::lock_recover(&self.passes, "Keep pass journal");
        if state.generation != chunk.generation {
            *state = Passes {
                generation: chunk.generation,
                ..Passes::default()
            };
        }
        let frames = i64::try_from(chunk.frames).ok();
        let raw_end = chunk
            .raw_host_start
            .zip(frames)
            .and_then(|(start, frames)| start.checked_add(frames));
        let aux_end = aux_point(chunk.auxiliary)
            .zip(frames)
            .and_then(|((source, start), frames)| Some((source, start.checked_add(frames)?)));
        let edge = PassEdge {
            raw_end,
            aux_end,
            in_trace: chunk.in_trace,
            after_offline_take: chunk.after_offline_take,
        };
        let summary_frames = if chunk.in_summary { chunk.frames } else { 0 };
        // A position that runs on keeps the pass, also across an auxiliary-only cut (some hosts
        // cut that clock at every callback while the take continues). Without host positions on
        // either side, the continuous clock (or nothing) keeps the pass.
        let continues = state.edge.is_some_and(|last| {
            last.in_trace == chunk.in_trace
                && last.after_offline_take == chunk.after_offline_take
                && match (last.raw_end, chunk.raw_host_start) {
                    (Some(end), Some(start)) => end == start,
                    (None, None) => {
                        last.aux_end.map(|(source, _)| source)
                            == edge.aux_end.map(|(source, _)| source)
                            && (last.aux_end.is_none()
                                || last.aux_end == aux_point(chunk.auxiliary))
                    }
                    _ => false,
                }
        });
        // The list keeps KEEP_PASS_LIMIT real passes; every pass beyond it joins one aggregate.
        let full = state.passes.len() >= KEEP_PASS_LIMIT;
        let aggregating = state.passes.last().is_some_and(|pass| {
            pass.trace_excluded_reason.as_deref() == Some(KEEP_PASS_MORE_PASSES)
        });
        let mut began_later_pass = false;
        match state.passes.last_mut().filter(|_| continues || aggregating) {
            Some(pass) => {
                pass.frames = pass.frames.saturating_add(chunk.frames);
                pass.summary_frames = pass.summary_frames.saturating_add(summary_frames);
                pass.host_end_position_samples = raw_end.filter(|_| !aggregating);
            }
            None if full => state.passes.push(KeepPass {
                host_start_position_samples: None,
                host_end_position_samples: None,
                frames: chunk.frames,
                summary_frames,
                in_trace: false,
                trace_excluded_reason: Some(KEEP_PASS_MORE_PASSES.to_string()),
            }),
            None => {
                let reason = (!chunk.in_trace).then(|| later_pass_reason(state.edge, &chunk));
                began_later_pass = reason.is_some()
                    && self
                        .later_pass_generation
                        .swap(chunk.generation, Ordering::AcqRel)
                        != chunk.generation;
                state.passes.push(KeepPass {
                    host_start_position_samples: chunk.raw_host_start,
                    host_end_position_samples: raw_end,
                    frames: chunk.frames,
                    summary_frames,
                    in_trace: chunk.in_trace,
                    trace_excluded_reason: reason.map(str::to_string),
                });
            }
        }
        state.edge = Some(edge);
        began_later_pass
    }

    /// IO Thread at Record close. A pass list with only the TRACE pass is omitted.
    pub(crate) fn passes(&self, generation: u64) -> Vec<KeepPass> {
        let state = crate::sync_recovery::lock_recover(&self.passes, "Keep pass journal");
        if state.generation != generation || state.passes.iter().all(|pass| pass.in_trace) {
            return Vec::new();
        }
        state.passes.clone()
    }
}

/// The samples the TRACE must cover in a record of `duration_samples`: all of them, except for a
/// Keep whose listed later realtime passes explain the rest of the record (for example an exported
/// WAV of the whole Keep). Its TRACE ends at its first pass by design, so the TRACE checks judge
/// that pass and a normal LOOP wrap degrades nothing. A span the passes do not explain keeps the
/// whole duration, so a real gap still degrades the record. Passes after an offline take are never
/// part of the record's audio and explain nothing.
pub fn trace_span_samples(passes: &[KeepPass], duration_samples: u64, sample_rate: u32) -> u64 {
    let (trace, later) = passes.iter().fold((0_u64, 0_u64), |(trace, later), pass| {
        if pass.in_trace {
            (trace.saturating_add(pass.frames), later)
        } else if pass.trace_excluded_reason.as_deref() == Some(KEEP_PASS_AFTER_OFFLINE_TAKE) {
            (trace, later)
        } else {
            (trace, later.saturating_add(pass.frames))
        }
    });
    // One TRACE slot of tolerance for where the record's edges fall inside a callback.
    let slot = u64::from(sample_rate / 10).max(1);
    if trace == 0
        || later == 0
        || trace.saturating_add(later).saturating_add(slot) < duration_samples
    {
        return duration_samples;
    }
    duration_samples.min(trace)
}

impl RecordTakeTracker {
    /// Measure Thread: account one consumed Record-lane chunk to its Keep pass.
    pub(crate) fn note_keep_chunk(&self, chunk: KeepChunk) {
        self.keep_passes.note(chunk);
    }

    /// IO Thread at Record close: every pass of the generation, in order.
    pub fn keep_passes(&self, generation: u64) -> Vec<KeepPass> {
        self.keep_passes.passes(generation)
    }

    /// Lock-free for the editor: this Record generation's TRACE ended at its first pass while
    /// the Keep went on into later passes.
    pub fn keep_trace_first_pass_only(&self, generation: u64) -> bool {
        generation != 0
            && self
                .keep_passes
                .later_pass_generation
                .load(Ordering::Acquire)
                == generation
    }
}

#[cfg(test)]
#[path = "keep_pass_journal_tests.rs"]
mod tests;
