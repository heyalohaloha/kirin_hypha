//! A single accepted event, source, logical window and producer completion stamp.
use crate::snapshot_types::*;
use crate::KirinAttackDetail;

#[path = "attack_single_v2_assemble.rs"]
mod assemble;
#[path = "attack_single_v2_request.rs"]
mod request;
pub use request::{
    kirin_hypha_cancel_attack_single_v2, kirin_hypha_poll_attack_single_v2,
    kirin_hypha_request_attack_single_v2,
};

pub const KIRIN_ATTACK_SINGLE_V2_VERSION: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KirinAttackSingleV2Request {
    pub version: u32,
    pub struct_size: u32,
    pub target: u8,
    pub band: u8,
    pub reserved: [u8; 6],
    pub event: KirinSnapshotEventKey,
}
impl KirinAttackSingleV2Request {
    pub fn is_valid(&self) -> bool {
        self.version == 2
            && self.struct_size as usize == std::mem::size_of::<Self>()
            && self.target <= KIRIN_TARGET_PRE
            && self.band <= 8
            && self.reserved == [0; 6]
            && self.event.source.is_valid()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackSingleSnapshotV2 {
    pub header: KirinSnapshotHeader,
    pub event: KirinSnapshotEventKey,
    pub request_token: u64,
    pub measurement_revision: u64,
    pub finish: u8,
    pub reason: u8,
    pub pair_kind: u8,
    pub has_all_pre: u8,
    pub has_all_post: u8,
    pub reserved: [u8; 3],
    /// BAND only: DELAY, ATT, REL, LEVEL. ALL uses the separately tagged unchanged raw detail.
    pub lanes: [KirinSnapshotScalarEvidence; 4],
    /// HEAD 96 points followed by TAIL 64. Values have meaning only at mask=1.
    pub pre: [f64; 160],
    pub post: [f64; 160],
    pub pre_valid: [u8; 160],
    pub post_valid: [u8; 160],
    pub all_pre: KirinAttackDetail,
    pub all_post: KirinAttackDetail,
}
impl Default for KirinAttackSingleSnapshotV2 {
    fn default() -> Self {
        Self {
            header: Default::default(),
            event: Default::default(),
            request_token: 0,
            measurement_revision: 0,
            finish: 0,
            reason: 0,
            pair_kind: 4,
            has_all_pre: 0,
            has_all_post: 0,
            reserved: [0; 3],
            lanes: [KirinSnapshotScalarEvidence::default(); 4],
            pre: [0.0; 160],
            post: [0.0; 160],
            pre_valid: [0; 160],
            post_valid: [0; 160],
            all_pre: Default::default(),
            all_post: Default::default(),
        }
    }
}

#[cfg(test)]
#[path = "attack_single_v2_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "attack_single_v2_boundary_tests.rs"]
mod boundary_tests;
