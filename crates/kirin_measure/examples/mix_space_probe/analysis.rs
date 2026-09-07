//! Development-only candidate. All policy parameters are explicit, unfrozen inputs.
use kirin_measure::{SuperFluxAnalyzer, SuperFluxChannelMode, SuperFluxConfig, SuperFluxFrame};
use serde::{Deserialize, Serialize};

#[path = "../space_decay_probe/analysis.rs"]
pub(crate) mod fixed;

#[derive(Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct Parameters {
    pub purpose: String,
    pub reference_window: u32,
    pub bands_per_octave: u32,
    pub maximum_filter_radius: usize,
    pub reference_dbfs: i32,
    pub peak_delta: f32,
    pub pre_max_hops: usize,
    pub pre_mean_hops: usize,
    pub refractory_ms: u32,
    pub floor_db: f64,
    pub floor_margin_db: f64,
    pub maximum_rise_db: f64,
    pub minimum_r_squared: f64,
    pub peak_search_ms: u32,
}

impl Parameters {
    pub fn validate(&self) -> Result<(), &'static str> {
        if self.purpose != "development_only"
            || !self.peak_delta.is_finite()
            || self.peak_delta <= 0.0
            || !(1..=100).contains(&self.pre_max_hops)
            || !(1..=200).contains(&self.pre_mean_hops)
            || !(1..=1000).contains(&self.refractory_ms)
            || !self.floor_db.is_finite()
            || !(-300.0..=0.0).contains(&self.floor_db)
            || !self.floor_margin_db.is_finite()
            || !(0.0..=60.0).contains(&self.floor_margin_db)
            || !self.maximum_rise_db.is_finite()
            || !(0.0..=20.0).contains(&self.maximum_rise_db)
            || !self.minimum_r_squared.is_finite()
            || !(0.0..=1.0).contains(&self.minimum_r_squared)
            || self.peak_search_ms > 80
            || !self.peak_search_ms.is_multiple_of(10)
        {
            return Err("invalid_or_non_development_parameters");
        }
        Ok(())
    }
}

#[derive(Debug, Serialize)]
pub(crate) struct Event {
    pub sample: usize,
    pub flux: f32,
}

pub(crate) fn detect(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    p: &Parameters,
) -> Result<(String, Vec<Event>), &'static str> {
    p.validate()?;
    if !matches!(channels, 1 | 2)
        || !pcm.len().is_multiple_of(channels)
        || pcm.iter().any(|v| !v.is_finite())
    {
        return Err("invalid_pcm");
    }
    let mut analyzer = SuperFluxAnalyzer::new(
        rate,
        SuperFluxConfig::new(
            p.reference_window,
            p.bands_per_octave,
            p.maximum_filter_radius,
            p.reference_dbfs,
            SuperFluxChannelMode::Lr,
            channels,
        ),
    )?;
    let definition = analyzer.layout().definition_hex();
    let window = analyzer.layout().window_samples;
    let hop = analyzer.layout().hop_samples;
    let frames = pcm.len() / channels;
    if frames < window {
        return Err("incomplete_analysis_window");
    }
    let mut left = vec![0.0; window];
    let mut right = vec![0.0; window];
    let mut trace = Vec::new();
    for start in (0..=frames - window).step_by(hop) {
        for i in 0..window {
            left[i] = pcm[(start + i) * channels];
            if channels == 2 {
                right[i] = pcm[(start + i) * channels + 1];
            }
        }
        if let Some(frame) = analyzer.analyze_window(
            &left,
            (channels == 2).then_some(right.as_slice()),
            start as i64,
        )? {
            trace.push(frame);
        }
    }
    Ok((definition, peaks(&trace, rate, p)))
}

pub(crate) fn peaks(trace: &[SuperFluxFrame], rate: u32, p: &Parameters) -> Vec<Event> {
    let mut selected: Vec<Event> = Vec::new();
    let refractory = (u64::from(rate) * u64::from(p.refractory_ms) + 500) / 1000;
    // Candidate policy: no zero-padded decision history and no synthesized final ODF frame.
    for index in p.pre_mean_hops.max(p.pre_max_hops)..trace.len() {
        let frame = trace[index];
        let mean = trace[index - p.pre_mean_hops..=index]
            .iter()
            .map(|v| v.value)
            .sum::<f32>()
            / (p.pre_mean_hops + 1) as f32;
        if frame.value < mean + p.peak_delta
            || trace[index - p.pre_max_hops..index]
                .iter()
                .any(|v| v.value >= frame.value)
        {
            continue;
        }
        let event = Event {
            sample: frame.event_sample as usize,
            flux: frame.value,
        };
        if let Some(previous) = selected.last_mut() {
            if event.sample - previous.sample <= refractory as usize {
                if event.flux > previous.flux {
                    *previous = event;
                }
                continue;
            }
        }
        selected.push(event);
    }
    selected
}

#[derive(Debug, Serialize)]
pub(crate) struct SpaceObservation {
    pub onset_sample: usize,
    pub next_onset_in_early_window: bool,
    pub fit_start_ms: u32,
    pub fit_end_ms: u32,
    pub stop_reason: &'static str,
    pub rejection: Option<&'static str>,
    pub facts: fixed::Facts,
}

pub(crate) fn observe(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    onset: usize,
    next_onset: Option<usize>,
    p: &Parameters,
) -> Result<SpaceObservation, &'static str> {
    p.validate()?;
    if !matches!(channels, 1 | 2)
        || !pcm.len().is_multiple_of(channels)
        || onset >= pcm.len() / channels
        || next_onset.is_some_and(|next| next <= onset)
    {
        return Err("invalid_event_range");
    }
    let needed = fixed::boundary(rate, 6000).ok_or("overflow")?;
    let remaining = pcm.len() / channels - onset;
    let tail = &pcm[onset * channels..(onset + needed.min(remaining)) * channels];
    // Validate native rate and finite PCM using the same calculator as manual research.
    fixed::analyze(tail, rate, channels, 0, 100, p.floor_db)?;
    let floor = 10.0_f64.powf((p.floor_db + p.floor_margin_db) / 10.0);
    let mut envelope: Vec<f64> = Vec::new();
    let mut stop = "six_second_bound";
    for ms in (0..6000).step_by(10) {
        let first = fixed::boundary(rate, ms).ok_or("overflow")?;
        let end = fixed::boundary(rate, ms + 10).ok_or("overflow")?;
        if end > tail.len() / channels {
            stop = "input_end";
            break;
        }
        if next_onset.is_some_and(|next| end > next - onset) {
            stop = "next_onset";
            break;
        }
        let power =
            fixed::power(&tail[first * channels..end * channels], channels) / (end - first) as f64;
        if power <= floor {
            stop = "floor_margin";
            break;
        }
        envelope.push(10.0 * power.log10());
    }
    // Select only the initial energy maximum in the explicit search window. Never search later
    // for the best-fitting line, bridge a rise/floor, or change EARLY's original fixed onset.
    let search = ((p.peak_search_ms / 10 + 1) as usize).min(envelope.len());
    let start = (0..search)
        .max_by(|&a, &b| envelope[a].total_cmp(&envelope[b]).then_with(|| b.cmp(&a)))
        .unwrap_or(0);
    let end = (start + 1..envelope.len())
        .find(|&i| envelope[i] - envelope[i - 1] > p.maximum_rise_db)
        .inspect(|_| stop = "energy_rise")
        .unwrap_or(envelope.len());
    let mut facts = fixed::analyze(
        tail,
        rate,
        channels,
        start as u32 * 10,
        end as u32 * 10,
        p.floor_db,
    )?;
    let rejection = match &facts.fit {
        None => facts.fit_reason,
        Some(fit) if fit.d20_seconds.is_none() => Some("insufficient_observed_fall"),
        Some(fit) if fit.r_squared < p.minimum_r_squared => Some("regression_fit"),
        Some(_) => None,
    };
    if rejection.is_some() {
        if let Some(fit) = facts.fit.as_mut() {
            fit.d20_seconds = None;
        }
    }
    Ok(SpaceObservation {
        onset_sample: onset,
        next_onset_in_early_window: next_onset
            .is_some_and(|next| next - onset < fixed::boundary(rate, 250).unwrap_or(0)),
        fit_start_ms: start as u32 * 10,
        fit_end_ms: end as u32 * 10,
        stop_reason: stop,
        rejection,
        facts,
    })
}
