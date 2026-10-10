//! Atomic TIME packet assembly. Sequential try-locks, cached comparison, no IO/recalculation.
use super::*;
#[path = "time_snapshot_abi.rs"]
mod abi;
pub use abi::*;
#[path = "time_snapshot_revision.rs"]
mod revision;
use kirin_measure::meter_delta_history::{TimeComparisonReason, TimeComparisonView};
use kirin_measure::meter_session::{TimeRawPoint, TimeSourceSpan};
use std::hash::{Hash, Hasher};
use std::time::Instant;

#[derive(Debug, PartialEq, Eq)]
struct Authority {
    pair: crate::pair_binding::PairObservationAuthority,
    signal: u8,
    role: Option<PluginDataRole>,
    post_id: String,
    post_project: String,
    owner: String,
    claim: u64,
    span_token: u64,
    audition_epoch: u64,
    audition_active: bool,
}
fn authority(engine: &KirinHyphaEngine) -> Option<Authority> {
    let pair = engine.pair_binding.try_observation_snapshot()?;
    let role = *engine.write_role.try_lock().ok()?;
    let post_id = engine.identity.try_lock().ok()?.instance_id.clone();
    let claim = engine.pair_claimed_at.try_read().ok()?.to_bits();
    let post_project = engine.project_hash_cell.try_read().ok()?.clone();
    Some(Authority {
        pair,
        signal: engine.signal_state_abi(),
        role,
        post_id,
        post_project,
        owner: engine.pair_owner.owner_id().into(),
        claim,
        span_token: engine.meter_delta_history.as_ref()?.time_post_span_token(),
        audition_epoch: engine.audition.epoch(),
        audition_active: engine.audition.is_active(),
    })
}
#[path = "level_comparison_values.rs"]
mod level;
pub(super) use level::level_values;
#[path = "time_comparison_current.rs"]
mod current;

fn opaque(value: impl Hash) -> u64 {
    let mut hash = std::collections::hash_map::DefaultHasher::new();
    value.hash(&mut hash);
    hash.finish()
}

fn comparison_matches(
    view: &TimeComparisonView,
    authority: &Authority,
    span: TimeSourceSpan,
) -> bool {
    let Some(exact) = authority.pair.exact.as_ref() else {
        return false;
    };
    view.binding_revision == exact.generation
        && view.pre_instance_id == exact.pre_instance_id
        && view.project_hash == exact.project_hash
        // The confirmed exact PRE locator may be in a different role-local project shelf.
        // Pair resolution owns scope admission; this packet must match that PRE locator.
        && view.owner_id == authority.owner
        && view.post_instance_id == authority.post_id
        && view.claimed_at_bits == authority.claim
        && view.post_span == span
}

fn absolute(
    point: Option<&TimeRawPoint>,
    target: u8,
    active: bool,
    now: Instant,
    span: TimeSourceSpan,
    count: usize,
) -> KirinTimeComponentV2 {
    let current = point
        .map(|p| KirinTimeCurrentV2::raw(p, target, active, now))
        .unwrap_or(KirinTimeCurrentV2 {
            span: span.into(),
            target,
            state: if active {
                KIRIN_TIME_CURRENT_WAITING
            } else {
                KIRIN_TIME_CURRENT_STOPPED
            },
            ..Default::default()
        });
    KirinTimeComponentV2 {
        history_count: count as u32,
        history_hold: u8::from(current.state != KIRIN_TIME_CURRENT_LIVE),
        current,
        ..Default::default()
    }
}

fn compared(
    view: Option<&TimeComparisonView>,
    authority: &Authority,
    span: TimeSourceSpan,
    local: Option<&TimeRawPoint>,
    active: bool,
    now: Instant,
    count: usize,
) -> KirinTimeComponentV2 {
    let mut component = absolute(None, KIRIN_TARGET_DELTA, active, now, span, count);
    component.binding_revision = authority.pair.generation;
    let Some(view) = view.filter(|v| comparison_matches(v, authority, span)) else {
        return component;
    };
    component.reason = view.reason as u8;
    component.current.cutoff = view.cutoff;
    component.pre_span = view.pre_span.into();
    component.pre_run = view.pre_run;
    component.locator_identity = opaque((&view.project_hash, &view.pre_instance_id));
    component.owner_identity = opaque((&view.owner_id, &view.post_instance_id));
    component.claim_identity = opaque(view.claimed_at_bits);
    if let Some(point) = &view.point {
        component.current = KirinTimeCurrentV2::raw(point, KIRIN_TARGET_DELTA, active, now);
        let fresh_axis = current::same_live_axis(view, span, local);
        if !fresh_axis || view.reason != TimeComparisonReason::Active {
            component.current.state = if active {
                KIRIN_TIME_CURRENT_MISSING
            } else {
                KIRIN_TIME_CURRENT_STOPPED
            };
            component.current.values = [f64::NAN; 6];
            component.current.finite_mask = 0;
        } else if let Some(latest) = local {
            // A known newer None is immediate absence, not normal publication lag.
            for i in 0..6 {
                if latest.wire.values[i].is_none() {
                    component.current.values[i] = f64::NAN;
                    component.current.finite_mask &= !(1 << i);
                }
            }
        }
    } else if view.reason != TimeComparisonReason::Waiting {
        component.current.state = if active {
            KIRIN_TIME_CURRENT_MISSING
        } else {
            KIRIN_TIME_CURRENT_STOPPED
        };
    }
    if component.current.state == KIRIN_TIME_CURRENT_LIVE && component.current.finite_mask == 0 {
        component.current.state = KIRIN_TIME_CURRENT_MISSING;
    }
    component.history_hold = u8::from(component.current.state != KIRIN_TIME_CURRENT_LIVE);
    component
}

fn psr_component(mut component: KirinTimeComponentV2) -> KirinTimeComponentV2 {
    component.current.finite_mask &= 1 << 3;
    for i in [0, 1, 2, 4, 5] {
        component.current.values[i] = f64::NAN;
    }
    if component.current.state == KIRIN_TIME_CURRENT_LIVE
        && component.current.finite_mask & (1 << 3) == 0
    {
        component.current.state = KIRIN_TIME_CURRENT_MISSING;
        component.history_hold = 1;
    }
    component
}

/// UI-only. Invalid inputs, contention and source/authority races leave all outputs unchanged.
/// # Safety
/// The handle is live; request names readable storage and outputs are disjoint writable buffers
/// of the declared sizes/capacities. Never call from an audio callback.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_time_snapshot_v2(
    handle: *const KirinHyphaEngine,
    request: *const KirinTimeSnapshotRequestV2,
    main: *mut KirinTimeHistoryEntryV2,
    main_capacity: u32,
    psr: *mut KirinTimeHistoryEntryV2,
    psr_capacity: u32,
    out: *mut KirinTimeSnapshotV2,
) -> u8 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null()
            || request.is_null()
            || out.is_null()
            || !(request as usize)
                .is_multiple_of(std::mem::align_of::<KirinTimeSnapshotRequestV2>())
            || !(out as usize).is_multiple_of(std::mem::align_of::<KirinTimeSnapshotV2>())
            || (main_capacity != 0
                && !(main as usize).is_multiple_of(std::mem::align_of::<KirinTimeHistoryEntryV2>()))
            || (psr_capacity != 0
                && !(psr as usize).is_multiple_of(std::mem::align_of::<KirinTimeHistoryEntryV2>()))
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        // Read only the fixed two-word prefix until the caller declares a complete request.
        let prefix = request.cast::<u32>();
        let version = unsafe { prefix.read() };
        let size = unsafe { prefix.add(1).read() };
        if version != KIRIN_TIME_SNAPSHOT_VERSION {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        }
        if size < std::mem::size_of::<KirinTimeSnapshotRequestV2>() as u32 {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        let request = unsafe { &*request };
        if request.version != KIRIN_TIME_SNAPSHOT_VERSION
            || request.struct_size < std::mem::size_of::<KirinTimeSnapshotRequestV2>() as u32
            || request.packet_size < std::mem::size_of::<KirinTimeSnapshotV2>() as u32
            || request.entry_size != std::mem::size_of::<KirinTimeHistoryEntryV2>() as u32
            || request.main_target > 2
            || request.resolution > 2
            || main_capacity > KIRIN_TIME_HISTORY_CAPACITY
            || psr_capacity > KIRIN_TIME_HISTORY_CAPACITY
            || (main_capacity != 0 && main.is_null())
            || (psr_capacity != 0 && psr.is_null())
        {
            return KIRIN_SNAPSHOT_INVALID_REQUEST;
        }
        let engine = unsafe { &*handle };
        let Some(before) = authority(engine) else {
            return KIRIN_SNAPSHOT_BUSY;
        };
        let is_post = before.role == Some(PluginDataRole::Post);
        let main_target = if is_post {
            request.main_target as u8
        } else {
            KIRIN_TARGET_PRE
        };
        if (is_post && main_target == KIRIN_TARGET_PRE)
            || (!is_post && request.main_target != u32::from(KIRIN_TARGET_PRE))
        {
            return KIRIN_SNAPSHOT_UNSUPPORTED;
        }
        let psr_target = if is_post {
            if before.pair.selection_intent {
                KIRIN_TARGET_DELTA
            } else {
                KIRIN_TARGET_POST
            }
        } else {
            KIRIN_TARGET_PRE
        };
        let resolution = match request.resolution {
            0 => MeterHistoryResolution::Hz10,
            1 => MeterHistoryResolution::Hz1,
            _ => MeterHistoryResolution::Hz0_1,
        };
        let Some(session) = engine
            .meter_session
            .as_ref()
            .and_then(|m| m.try_lock().ok())
        else {
            return KIRIN_SNAPSHOT_BUSY;
        };
        let span = session.time_source_span();
        let snapshot = session.snapshot();
        let local = session.time_raw_tail(1).pop();
        let cutoff = snapshot.observed_frames;
        let lower = cutoff.saturating_sub(request.duration_frames);
        let local_main = if main_target != KIRIN_TARGET_DELTA {
            match session.time_history(resolution, lower, cutoff, main_capacity as usize) {
                Ok(history) => history,
                Err(_) => return KIRIN_SNAPSHOT_UNSUPPORTED,
            }
        } else {
            Vec::new()
        };
        let local_psr = if psr_target != KIRIN_TARGET_DELTA {
            match session.time_history(resolution, lower, cutoff, psr_capacity as usize) {
                Ok(history) => history,
                Err(_) => return KIRIN_SNAPSHOT_UNSUPPORTED,
            }
        } else {
            Vec::new()
        };
        drop(session);
        if span.token != before.span_token
            || local
                .as_ref()
                .is_some_and(|p| p.wire.span != span || p.wire.observed != cutoff)
        {
            return KIRIN_SNAPSHOT_RETIRED;
        }
        let psr_requested = psr_capacity != 0;
        let mut comparison = None;
        if main_target == KIRIN_TARGET_DELTA || (psr_requested && psr_target == KIRIN_TARGET_DELTA)
        {
            let Some(exchange) = engine.meter_delta_history.as_ref() else {
                return KIRIN_SNAPSHOT_BUSY;
            };
            let Some(view) = exchange.time_comparison(
                resolution,
                lower,
                cutoff,
                (if main_target == KIRIN_TARGET_DELTA {
                    main_capacity
                } else {
                    0
                })
                .max(if psr_requested && psr_target == KIRIN_TARGET_DELTA {
                    psr_capacity
                } else {
                    0
                }) as usize,
            ) else {
                return KIRIN_SNAPSHOT_BUSY;
            };
            let view = match view {
                Ok(view) => view,
                Err(_) => return KIRIN_SNAPSHOT_UNSUPPORTED,
            };
            comparison = view.filter(|v| comparison_matches(v, &before, span));
            if comparison.as_ref().is_some_and(|v| {
                v.cutoff > cutoff
                    || v.point
                        .as_ref()
                        .is_some_and(|p| p.wire.observed > cutoff || p.wire.span != span)
            }) {
                return KIRIN_SNAPSHOT_RETIRED;
            }
        }
        let delta_history = comparison
            .as_ref()
            .map_or(&[][..], |v| v.history.as_slice());
        let main_entries = if main_target == KIRIN_TARGET_DELTA {
            match kirin_measure::meter_history::reduce_time_history(
                delta_history,
                main_capacity as usize,
                resolution,
            ) {
                Ok(history) => history,
                Err(_) => return KIRIN_SNAPSHOT_UNSUPPORTED,
            }
        } else {
            local_main
        };
        let psr_entries = if psr_target == KIRIN_TARGET_DELTA {
            match kirin_measure::meter_history::reduce_time_history(
                delta_history,
                psr_capacity as usize,
                resolution,
            ) {
                Ok(history) => history,
                Err(_) => return KIRIN_SNAPSHOT_UNSUPPORTED,
            }
        } else {
            local_psr
        };
        if main_entries.iter().chain(&psr_entries).any(|p| {
            p.measurement_epoch != span.epoch
                || p.generation != span.generation
                || p.last_observed_frames > cutoff
        }) {
            return KIRIN_SNAPSHOT_BUSY;
        }
        let now = Instant::now();
        let active = before.signal == 1 && snapshot.state == MeterSessionState::Active;
        let main_component = if main_target == KIRIN_TARGET_DELTA {
            compared(
                comparison.as_ref(),
                &before,
                span,
                local.as_ref(),
                active,
                now,
                main_entries.len(),
            )
        } else {
            absolute(
                local.as_ref(),
                main_target,
                active,
                now,
                span,
                main_entries.len(),
            )
        };
        let psr_component = psr_component(if !psr_requested {
            absolute(None, psr_target, active, now, span, 0)
        } else if psr_target == KIRIN_TARGET_DELTA {
            compared(
                comparison.as_ref(),
                &before,
                span,
                local.as_ref(),
                active,
                now,
                psr_entries.len(),
            )
        } else {
            absolute(
                local.as_ref(),
                psr_target,
                active,
                now,
                span,
                psr_entries.len(),
            )
        });
        let packet = KirinTimeSnapshotV2 {
            version: KIRIN_TIME_SNAPSHOT_VERSION,
            struct_size: std::mem::size_of::<KirinTimeSnapshotV2>() as u32,
            revision: revision::content_revision(
                &main_component,
                &psr_component,
                &main_entries,
                &psr_entries,
                (
                    span,
                    cutoff,
                    lower,
                    before.pair.generation,
                    before.signal,
                    before.pair.selection_intent,
                ),
            ),
            local_cutoff: cutoff,
            range_start: lower,
            post_span: span.into(),
            binding_revision: before.pair.generation,
            signal_state: before.signal,
            selection_intent: u8::from(before.pair.selection_intent),
            main: main_component,
            psr: psr_component,
            ..Default::default()
        };
        #[cfg(test)]
        tests::at_boundary(engine);
        let Some(after) = authority(engine) else {
            return KIRIN_SNAPSHOT_BUSY;
        };
        if before != after {
            return KIRIN_SNAPSHOT_RETIRED;
        }
        for (i, entry) in main_entries.into_iter().enumerate() {
            unsafe {
                main.add(i).write(entry.into());
            }
        }
        for (i, entry) in psr_entries.into_iter().enumerate() {
            unsafe {
                psr.add(i).write(entry.into());
            }
        }
        unsafe {
            out.write(packet);
        }
        KIRIN_SNAPSHOT_SUCCESS
    }))
    .unwrap_or(KIRIN_SNAPSHOT_BUSY)
}

#[cfg(test)]
#[path = "time_comparison_current_tests.rs"]
mod current_tests;
#[cfg(test)]
#[path = "time_snapshot_ffi_tests.rs"]
mod tests;
