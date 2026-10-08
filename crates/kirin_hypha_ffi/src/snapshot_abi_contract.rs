//! Sized handshake for the independent observation snapshots. Legacy ABI stays unchanged.
use std::mem::{align_of, size_of};

use crate::{KirinAttackBandSummaryV2, KirinAttackSingleSnapshotV2, KirinTimeSnapshotV2};

pub const KIRIN_SNAPSHOT_ABI_VERSION: u32 = 1;

#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct KirinSnapshotAbiContract {
    pub version: u32,
    pub struct_size: u32,
    pub legacy_revision: u32,
    pub drum_cohort_capacity: u32,
    pub time_raw_capacity: u32,
    pub time_history_capacity: u32,
    pub summary_version: u32,
    pub single_version: u32,
    pub time_version: u32,
    pub reserved: u32,
    pub summary_size: u64,
    pub summary_align: u64,
    pub single_size: u64,
    pub single_align: u64,
    pub time_size: u64,
    pub time_align: u64,
    pub common_offsets: [u32; 12],
    pub summary_offsets: [u32; 8],
    pub single_offsets: [u32; 10],
    pub time_offsets: [u32; 8],
    pub enum_revision: u32,
    pub layout_reserved: u32,
}

pub fn snapshot_abi_contract() -> KirinSnapshotAbiContract {
    KirinSnapshotAbiContract {
        version: KIRIN_SNAPSHOT_ABI_VERSION,
        struct_size: size_of::<KirinSnapshotAbiContract>() as u32,
        legacy_revision: super::KIRIN_ABI_REVISION,
        drum_cohort_capacity: 8,
        time_raw_capacity: 64,
        time_history_capacity: 1200,
        summary_version: 2,
        single_version: 2,
        time_version: 2,
        reserved: 0,
        summary_size: size_of::<KirinAttackBandSummaryV2>() as u64,
        summary_align: align_of::<KirinAttackBandSummaryV2>() as u64,
        single_size: size_of::<KirinAttackSingleSnapshotV2>() as u64,
        single_align: align_of::<KirinAttackSingleSnapshotV2>() as u64,
        time_size: size_of::<KirinTimeSnapshotV2>() as u64,
        time_align: align_of::<KirinTimeSnapshotV2>() as u64,
        common_offsets: common_offsets(),
        summary_offsets: summary_offsets(),
        single_offsets: single_offsets(),
        time_offsets: time_offsets(),
        enum_revision: 1,
        layout_reserved: 0,
    }
}

fn common_offsets() -> [u32; 12] {
    use crate::{
        KirinSnapshotEventKey as E, KirinSnapshotHeader as H, KirinSnapshotInterval as I,
        KirinSnapshotScalarEvidence as V, KirinSnapshotSourceKey as S,
    };
    use std::mem::offset_of;
    [
        offset_of!(S, generation),
        offset_of!(S, sample_rate),
        offset_of!(S, channels),
        offset_of!(S, odf_hash),
        offset_of!(E, event_sample),
        offset_of!(E, token),
        offset_of!(I, upper),
        offset_of!(I, unit),
        offset_of!(V, measurement_revision),
        offset_of!(V, class),
        offset_of!(H, source),
        offset_of!(H, band_semantic_hash),
    ]
    .map(|v| v as u32)
}
fn summary_offsets() -> [u32; 8] {
    use crate::{KirinAttackBandSummaryV2 as S, KirinAttackBandSummaryV2Request as R};
    use std::mem::offset_of;
    [
        offset_of!(S, cohort_count),
        offset_of!(S, events),
        offset_of!(S, pair_kind),
        offset_of!(S, evidence),
        offset_of!(S, lanes),
        offset_of!(S, head),
        offset_of!(S, tail),
        offset_of!(R, target),
    ]
    .map(|v| v as u32)
}
fn single_offsets() -> [u32; 10] {
    use crate::KirinAttackSingleSnapshotV2 as S;
    use std::mem::offset_of;
    [
        offset_of!(S, event),
        offset_of!(S, request_token),
        offset_of!(S, measurement_revision),
        offset_of!(S, lanes),
        offset_of!(S, pre),
        offset_of!(S, post),
        offset_of!(S, pre_valid),
        offset_of!(S, post_valid),
        offset_of!(S, all_pre),
        offset_of!(S, all_post),
    ]
    .map(|v| v as u32)
}
fn time_offsets() -> [u32; 8] {
    use crate::{
        KirinTimeComponentV2 as P, KirinTimeCurrentV2 as C, KirinTimeHistoryEntryV2 as E,
        KirinTimeSnapshotRequestV2 as R, KirinTimeSnapshotV2 as S,
    };
    use std::mem::offset_of;
    [
        offset_of!(C, values),
        offset_of!(P, pre_span),
        offset_of!(P, binding_revision),
        offset_of!(S, main),
        offset_of!(S, psr),
        offset_of!(E, ranges),
        offset_of!(E, valid_count),
        offset_of!(R, main_target),
    ]
    .map(|v| v as u32)
}

/// A failed handshake never writes even the version/size prefix.
///
/// # Safety
/// A non-null output names writable storage of `out_size` bytes, aligned for this type.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_snapshot_abi_contract(
    version: u32,
    out_size: u32,
    out: *mut KirinSnapshotAbiContract,
) -> u8 {
    if version != KIRIN_SNAPSHOT_ABI_VERSION {
        return crate::KIRIN_SNAPSHOT_UNSUPPORTED;
    }
    if out_size < size_of::<KirinSnapshotAbiContract>() as u32
        || out.is_null()
        || !(out as usize).is_multiple_of(align_of::<KirinSnapshotAbiContract>())
    {
        return crate::KIRIN_SNAPSHOT_INVALID_REQUEST;
    }
    unsafe { out.write(snapshot_abi_contract()) };
    crate::KIRIN_SNAPSHOT_SUCCESS
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn independent_handshake_layout_preserves_legacy_contract() {
        assert_eq!(
            [
                crate::KIRIN_TARGET_POST,
                crate::KIRIN_TARGET_DELTA,
                crate::KIRIN_TARGET_PRE
            ],
            [0, 1, 2]
        );
        assert_eq!(
            [
                crate::KIRIN_SNAPSHOT_SUCCESS,
                crate::KIRIN_SNAPSHOT_BUSY,
                crate::KIRIN_SNAPSHOT_INVALID_REQUEST,
                crate::KIRIN_SNAPSHOT_UNSUPPORTED,
                crate::KIRIN_SNAPSHOT_RETIRED
            ],
            [0, 1, 2, 3, 4]
        );
        assert_eq!(size_of::<KirinSnapshotAbiContract>(), 248);
        assert_eq!(align_of::<KirinSnapshotAbiContract>(), 8);
        assert_eq!(
            std::mem::offset_of!(KirinSnapshotAbiContract, summary_size),
            40
        );
        assert_eq!(
            std::mem::offset_of!(KirinSnapshotAbiContract, time_align),
            80
        );
        assert_eq!(size_of::<super::super::KirinAbiContract>(), 112);
        assert_eq!(super::super::KIRIN_ABI_REVISION, 7);
        assert_eq!(
            common_offsets(),
            [16, 24, 28, 32, 64, 72, 16, 32, 40, 96, 40, 104]
        );
        assert_eq!(summary_offsets(), [136, 144, 784, 792, 4120, 4568, 9944, 8]);
        assert_eq!(
            single_offsets(),
            [136, 216, 224, 240, 656, 1936, 3216, 3376, 3536, 4048]
        );
        assert_eq!(time_offsets(), [80, 136, 184, 88, 312, 64, 208, 24]);
    }

    #[test]
    fn summary_payload_layout_is_pinned_independently_of_generated_handshake() {
        use crate::{
            KirinAttackBandAveragePointV2 as Average, KirinAttackBandLaneSummaryV2 as Lane,
            KirinAttackBandSummaryV2 as Summary, KirinAttackBandSummaryV2Request as Request,
        };
        use std::mem::offset_of;
        assert_eq!(size_of::<Request>(), 16);
        assert_eq!(size_of::<Lane>(), 112);
        assert_eq!(size_of::<Average>(), 56);
        assert_eq!(size_of::<Summary>(), 13528);
        assert_eq!(offset_of!(Lane, whole_interval), 8);
        assert_eq!(offset_of!(Lane, exact_median), 48);
        assert_eq!(offset_of!(Lane, reason_count), 72);
        assert_eq!(offset_of!(Average, post_mean), 32);
        assert_eq!(offset_of!(Summary, events), 144);
        assert_eq!(offset_of!(Summary, evidence), 792);
        assert_eq!(offset_of!(Summary, lanes), 4120);
        assert_eq!(offset_of!(Summary, head), 4568);
        assert_eq!(offset_of!(Summary, tail), 9944);
    }

    #[test]
    fn time_payload_layout_is_pinned_independently_of_generated_handshake() {
        use crate::{
            KirinTimeComponentV2 as Component, KirinTimeCurrentV2 as Current,
            KirinTimeHistoryEntryV2 as Entry, KirinTimeSnapshotRequestV2 as Request,
            KirinTimeSnapshotV2 as Packet, KirinTimeSourceSpanV2 as Span,
        };
        use std::mem::offset_of;
        assert_eq!(size_of::<Span>(), 40);
        assert_eq!(size_of::<Current>(), 136);
        assert_eq!(size_of::<Component>(), 224);
        assert_eq!(size_of::<Packet>(), 536);
        assert_eq!(size_of::<Entry>(), 224);
        assert_eq!(size_of::<Request>(), 32);
        assert_eq!(offset_of!(Current, values), 80);
        assert_eq!(offset_of!(Component, pre_span), 136);
        assert_eq!(offset_of!(Component, binding_revision), 184);
        assert_eq!(offset_of!(Packet, main), 88);
        assert_eq!(offset_of!(Packet, psr), 312);
        assert_eq!(offset_of!(Entry, ranges), 64);
        assert_eq!(offset_of!(Entry, valid_count), 208);
    }

    #[test]
    fn single_payload_layout_is_pinned_independently_of_generated_handshake() {
        use crate::{
            KirinAttackDetail as Detail, KirinAttackSingleSnapshotV2 as Single,
            KirinAttackSingleV2Request as Request,
        };
        use std::mem::offset_of;
        assert_eq!(size_of::<Single>(), 4560);
        assert_eq!(size_of::<Request>(), 96);
        assert_eq!(size_of::<Detail>(), 512);
        assert_eq!(offset_of!(Single, event), 136);
        assert_eq!(offset_of!(Single, request_token), 216);
        assert_eq!(offset_of!(Single, lanes), 240);
        assert_eq!(offset_of!(Single, pre), 656);
        assert_eq!(offset_of!(Single, post), 1936);
        assert_eq!(offset_of!(Single, pre_valid), 3216);
        assert_eq!(offset_of!(Single, post_valid), 3376);
        assert_eq!(offset_of!(Single, all_pre), 3536);
        assert_eq!(offset_of!(Single, all_post), 4048);
    }

    #[test]
    fn unknown_version_short_buffer_and_null_leave_all_canaries_unchanged() {
        let mut output = KirinSnapshotAbiContract {
            summary_size: 0x1357_2468_9876_abcd,
            time_align: u64::MAX,
            ..Default::default()
        };
        let original = output;
        for (version, bytes) in [(0, 248), (2, 248), (u32::MAX, 248), (1, 0), (1, 247)] {
            assert_eq!(
                unsafe { kirin_hypha_snapshot_abi_contract(version, bytes, &mut output) },
                if version == 1 {
                    crate::KIRIN_SNAPSHOT_INVALID_REQUEST
                } else {
                    crate::KIRIN_SNAPSHOT_UNSUPPORTED
                }
            );
            assert_eq!(output, original);
        }
        assert_eq!(
            unsafe { kirin_hypha_snapshot_abi_contract(1, 248, std::ptr::null_mut()) },
            crate::KIRIN_SNAPSHOT_INVALID_REQUEST
        );
        assert_eq!(
            unsafe { kirin_hypha_snapshot_abi_contract(1, 248, &mut output) },
            crate::KIRIN_SNAPSHOT_SUCCESS
        );
        assert_eq!(output, snapshot_abi_contract());
    }
}
