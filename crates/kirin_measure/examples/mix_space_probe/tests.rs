use super::analysis::*;
use kirin_measure::SuperFluxFrame;

#[test]
fn candidate_peaks_need_real_history_and_keep_strongest_inside_refractory() {
    let mut trace = (0..60)
        .map(|n| SuperFluxFrame {
            support_start_samples: n * 256,
            support_end_samples: n * 256 + 2048,
            event_sample: n * 256 + 1024,
            value: 0.0,
        })
        .collect::<Vec<_>>();
    trace[1].value = 1.0; // Not enough real decision history: do not fabricate it.
    trace[30].value = 0.5;
    trace[31].value = 0.8;
    trace[32].value = 0.6;
    trace[40].value = 0.4;
    let events = peaks(&trace, 48000, &parameters());
    assert_eq!(events.len(), 2);
    assert_eq!(events[0].sample, trace[31].event_sample as usize);
    assert_eq!(events[1].sample, trace[40].event_sample as usize);
    assert_eq!(events[0].flux, 0.8);
}

fn parameters() -> Parameters {
    // Synthetic diagnostic parameters only; not a frozen 2MIX definition or winner.
    serde_json::from_str(
        r#"{"purpose":"development_only","reference_window":2048,
        "bands_per_octave":24,"maximum_filter_radius":1,"reference_dbfs":-70,
        "peak_delta":0.02,"pre_max_hops":3,"pre_mean_hops":24,"refractory_ms":30,
        "floor_db":-90,"floor_margin_db":6,"maximum_rise_db":1,
        "minimum_r_squared":0.98,"peak_search_ms":80}"#,
    )
    .unwrap()
}
fn tail(rate: u32) -> Vec<f32> {
    (0..rate)
        .map(|n| (0.5 * 10.0_f64.powf(-40.0 * f64::from(n) / f64::from(rate) / 20.0)) as f32)
        .collect()
}
#[test]
fn known_decay_selects_interval_and_preserves_fixed_early_definition() {
    for rate in [8000, 44150, 48000, 192000, 768000] {
        let pcm = tail(rate);
        let value = observe(&pcm, rate, 1, 0, None, &parameters()).unwrap();
        assert_eq!(value.fit_start_ms, 0);
        assert_eq!(value.fit_end_ms, 1000);
        assert!(value.rejection.is_none());
        assert!((value.facts.fit.unwrap().d20_seconds.unwrap() - 0.5).abs() < 1e-6);
        let manual = fixed::analyze(&pcm, rate, 1, 0, 1000, -90.0).unwrap();
        assert_eq!(value.facts.early_db, manual.early_db);
    }
}
#[test]
fn silence_and_constant_signal_do_not_invent_events_or_decay() {
    for level in [0.0, 0.5] {
        let pcm = vec![level; 48000];
        assert!(detect(&pcm, 48000, 1, &parameters()).unwrap().1.is_empty());
        let value = observe(&pcm, 48000, 1, 0, None, &parameters()).unwrap();
        assert!(value.rejection.is_some());
        assert!(value.facts.fit.is_none_or(|fit| fit.d20_seconds.is_none()));
    }
}
#[test]
fn next_event_and_energy_rise_end_the_interval_without_stitching() {
    let mut pcm = tail(48000);
    let value = observe(&pcm, 48000, 1, 0, Some(9600), &parameters()).unwrap();
    assert!(value.next_onset_in_early_window);
    assert_eq!(value.fit_end_ms, 200);
    assert_eq!(value.stop_reason, "next_onset");
    assert!(value.rejection.is_some());
    pcm[14400..].fill(0.8);
    let value = observe(&pcm, 48000, 1, 0, None, &parameters()).unwrap();
    assert_eq!(value.stop_reason, "energy_rise");
    assert_eq!(value.fit_end_ms, 300);
    assert!(value.rejection.is_some());
}
#[test]
fn invalid_parameters_pcm_and_non_development_purpose_are_refused() {
    let mut p = parameters();
    p.purpose = "holdout".into();
    assert!(p.validate().is_err());
    p = parameters();
    p.minimum_r_squared = f64::NAN;
    assert!(p.validate().is_err());
    assert!(detect(&[f32::NAN; 4096], 48000, 1, &parameters()).is_err());
    assert!(detect(&[], 48000, 1, &parameters()).is_err());
    assert!(observe(&[0.0; 10], 48000, 1, 10, None, &parameters()).is_err());
    assert!(observe(&tail(48000), 48000, 1, 4, Some(3), &parameters()).is_err());
}
#[test]
fn stereo_antiphase_preserves_energy_and_a_fixed_onset_gain_difference() {
    let pcm = tail(48000);
    let stereo = pcm
        .iter()
        .flat_map(|&x| [x * 0.5, -x * 0.5])
        .collect::<Vec<_>>();
    let a = observe(&pcm, 48000, 1, 0, None, &parameters()).unwrap();
    let b = observe(&stereo, 48000, 2, 0, None, &parameters()).unwrap();
    assert!((a.facts.early_db.unwrap() - b.facts.early_db.unwrap()).abs() < 1e-8);
    assert!(
        (a.facts.fit.unwrap().d20_seconds.unwrap() - b.facts.fit.unwrap().d20_seconds.unwrap())
            .abs()
            < 1e-8
    );
}
