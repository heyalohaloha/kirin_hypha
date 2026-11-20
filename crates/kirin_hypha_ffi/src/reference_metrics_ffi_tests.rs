use super::*;

#[test]
fn reference_metric_frame_has_independent_stable_layout() {
    assert_eq!(std::mem::size_of::<KirinReferenceMetricFrame>(), 32);
    assert_eq!(
        std::mem::offset_of!(KirinReferenceMetricFrame, end_sample),
        0
    );
    assert_eq!(std::mem::offset_of!(KirinReferenceMetricFrame, crest_db), 8);
    assert_eq!(std::mem::offset_of!(KirinReferenceMetricFrame, psr_db), 16);
    assert_eq!(
        std::mem::offset_of!(KirinReferenceMetricFrame, sharpness_acum),
        24
    );
}

#[test]
fn reference_metrics_ffi_validates_before_advancing_and_preserves_output_on_error() {
    assert!(kirin_reference_metrics_create(48_000, 3).is_null());
    let handle = kirin_reference_metrics_create(48_000, 1);
    assert!(!handle.is_null());
    let mut output = [KirinReferenceMetricFrame {
        end_sample: 999,
        crest_db: 999.0,
        psr_db: 999.0,
        sharpness_acum: 999.0,
    }; 8];
    let input = vec![0.0; 4800];
    let mut written = 99;
    unsafe {
        assert!(!kirin_reference_metrics_push(
            std::ptr::null_mut(),
            input.as_ptr(),
            input.len(),
            output.as_mut_ptr(),
            8,
            &mut written
        ));
        assert!(!kirin_reference_metrics_push(
            handle,
            std::ptr::null(),
            input.len(),
            output.as_mut_ptr(),
            8,
            &mut written
        ));
        assert!(!kirin_reference_metrics_push(
            handle,
            input.as_ptr(),
            input.len(),
            output.as_mut_ptr(),
            7,
            &mut written
        ));
        assert!(!kirin_reference_metrics_push(
            handle,
            input.as_ptr(),
            input.len() - 1,
            output.as_mut_ptr(),
            8,
            &mut written
        ));
        assert_eq!(written, 99);
        assert_eq!(output[0].end_sample, 999);
        assert!(kirin_reference_metrics_push(
            handle,
            input.as_ptr(),
            input.len(),
            output.as_mut_ptr(),
            8,
            &mut written
        ));
        assert_eq!(written, 1);
        assert_eq!(output[0].end_sample, 4800);
        assert!(output[0].crest_db.is_nan());
        assert!(output[0].psr_db.is_nan());
        assert_eq!(output[1].end_sample, 999);
        (*handle).poisoned = true;
        written = 88;
        assert!(!kirin_reference_metrics_push(
            handle,
            input.as_ptr(),
            input.len(),
            output.as_mut_ptr(),
            8,
            &mut written
        ));
        assert_eq!(written, 88);
        kirin_reference_metrics_drop(handle);
        kirin_reference_metrics_drop(std::ptr::null_mut());
    }
}

#[test]
fn reference_metrics_ffi_eof_and_null_output_are_explicit() {
    for rate in [44_100, 48_000, 96_000] {
        let handle = kirin_reference_metrics_create(rate, 1);
        assert!(!handle.is_null());
        let mut output = [KirinReferenceMetricFrame {
            end_sample: 999,
            crest_db: 999.0,
            psr_db: 999.0,
            sharpness_acum: 999.0,
        }; 8];
        let input = vec![0.25; (rate / 10) as usize];
        let mut written = 99;
        unsafe {
            assert!(!kirin_reference_metrics_push(
                handle,
                input.as_ptr(),
                input.len(),
                std::ptr::null_mut(),
                8,
                &mut written
            ));
            assert!(!kirin_reference_metrics_push(
                handle,
                input.as_ptr(),
                input.len(),
                output.as_mut_ptr(),
                8,
                std::ptr::null_mut()
            ));
            assert!(!kirin_reference_metrics_push(
                handle,
                input.as_ptr(),
                0,
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            assert!(!kirin_reference_metrics_push(
                handle,
                input.as_ptr(),
                76_801,
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            assert_eq!(written, 99);
            assert!(kirin_reference_metrics_push(
                handle,
                input.as_ptr(),
                input.len(),
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            let before = written;
            assert!(!kirin_reference_metrics_finish(
                std::ptr::null_mut(),
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            assert!(!kirin_reference_metrics_finish(
                handle,
                std::ptr::null_mut(),
                8,
                &mut written
            ));
            assert!(!kirin_reference_metrics_finish(
                handle,
                output.as_mut_ptr(),
                8,
                std::ptr::null_mut()
            ));
            assert!(!kirin_reference_metrics_finish(
                handle,
                output.as_mut_ptr(),
                7,
                &mut written
            ));
            assert_eq!(written, before);
            assert!(kirin_reference_metrics_finish(
                handle,
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            assert_eq!(before + written, 1);
            if written > 0 {
                assert_eq!(output[0].end_sample, u64::from(rate / 10));
                assert!(output[0].sharpness_acum.is_nan());
            }
            assert!(kirin_reference_metrics_finish(
                handle,
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            assert_eq!(written, 0);
            assert!(!kirin_reference_metrics_push(
                handle,
                input.as_ptr(),
                input.len(),
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            (*handle).poisoned = true;
            written = 77;
            assert!(!kirin_reference_metrics_finish(
                handle,
                output.as_mut_ptr(),
                8,
                &mut written
            ));
            assert_eq!(written, 77);
            kirin_reference_metrics_drop(handle);
        }
    }
}

#[test]
fn reference_metrics_ffi_absolute_origin_validation() {
    assert!(kirin_reference_metrics_create_at(48000, 2, 1).is_null());
    assert!(kirin_reference_metrics_create_at(48000, 2, u64::MAX).is_null());
    let handle = kirin_reference_metrics_create_at(48000, 1, 9600);
    assert!(!handle.is_null());
    unsafe {
        let mut out = [KirinReferenceMetricFrame {
            end_sample: 0,
            crest_db: 0.0,
            psr_db: 0.0,
            sharpness_acum: 0.0,
        }; 8];
        let mut written = 0;
        assert!(kirin_reference_metrics_push(
            handle,
            [0.0f32; 4800].as_ptr(),
            4800,
            out.as_mut_ptr(),
            8,
            &mut written
        ));
        if written != 0 {
            assert_eq!(out[0].end_sample, 14400);
        }
        kirin_reference_metrics_drop(handle);
    }
}
