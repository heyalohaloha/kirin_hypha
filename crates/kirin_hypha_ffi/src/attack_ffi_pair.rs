//! Exact PRE/POST ATTACK presentation DTOs and polling entry points.

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::PluginDataRole;

use super::convert::{
    to_c_attack_detail_batch, to_c_attack_pair_event_batch, to_c_attack_waveform_batch,
};
use super::{KirinAttackDetailBatch, KirinAttackWaveformBatch, KirinHyphaEngine};

pub const KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY: usize = 240;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackPairEvent {
    pub pair_generation: u64,
    pub pre_generation: u64,
    pub post_generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub kind: u8,
    pub pre_available: u8,
    pub post_available: u8,
    pub definition_hash: [u8; 32],
    pub event_sample: i64,
    pub decision_sample: i64,
    pub pre_event_sample: i64,
    pub post_event_sample: i64,
    pub pre_value: f32,
    pub post_value: f32,
    pub delta_value: f32,
    pub delta_available: u8,
    pub reserved: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinAttackPairEventBatch {
    pub status: u8,
    pub reserved: [u8; 3],
    pub count: u32,
    pub capacity: u32,
    pub reserved2: u32,
    pub events: [KirinAttackPairEvent; KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY],
}

impl Default for KirinAttackPairEventBatch {
    fn default() -> Self {
        Self {
            status: 0,
            reserved: [0; 3],
            count: 0,
            capacity: KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY as u32,
            reserved2: 0,
            events: [KirinAttackPairEvent::default(); KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY],
        }
    }
}

impl KirinHyphaEngine {
    pub fn poll_attack_pre_waveform(&self) -> Option<KirinAttackWaveformBatch> {
        self.attack_pair_view()?.pre.map(to_c_attack_waveform_batch)
    }

    pub fn poll_attack_pre_details(&self) -> Option<KirinAttackDetailBatch> {
        self.attack_pair_view()?.pre.map(to_c_attack_detail_batch)
    }

    pub fn poll_attack_pair_events(&self) -> Option<KirinAttackPairEventBatch> {
        self.attack_pair_view().map(to_c_attack_pair_event_batch)
    }

    fn attack_pair_view(&self) -> Option<kirin_measure::AttackPairViewSnapshot> {
        if self.write_role.lock().ok().and_then(|role| *role) != Some(PluginDataRole::Post) {
            return None;
        }
        self.spectrum.try_attack_view()
    }
}

macro_rules! attack_pair_poll {
    ($name:ident, $method:ident, $output:ty) => {
        #[doc = "# Safety\n\n`handle` must be either null or a live engine created by this ABI, and `out` must be either null or point to writable storage for the complete output type."]
        #[no_mangle]
        pub unsafe extern "C" fn $name(handle: *mut KirinHyphaEngine, out: *mut $output) -> bool {
            catch_unwind(AssertUnwindSafe(|| {
                if handle.is_null() || out.is_null() {
                    return false;
                }
                let Some(batch) = (unsafe { &*handle }).$method() else {
                    return false;
                };
                unsafe { *out = batch };
                true
            }))
            .unwrap_or(false)
        }
    };
}

attack_pair_poll!(
    kirin_hypha_poll_attack_pre_waveform,
    poll_attack_pre_waveform,
    KirinAttackWaveformBatch
);
attack_pair_poll!(
    kirin_hypha_poll_attack_pre_details,
    poll_attack_pre_details,
    KirinAttackDetailBatch
);
attack_pair_poll!(
    kirin_hypha_poll_attack_pair_events,
    poll_attack_pair_events,
    KirinAttackPairEventBatch
);
