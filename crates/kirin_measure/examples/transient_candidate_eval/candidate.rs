use std::fs;
use std::path::Path;

use kirin_measure::{SuperFluxChannelMode, SuperFluxConfig, TransientOdfKind};
use serde::{Deserialize, Serialize};

use super::sha256_bytes;

const SUPERFLUX_DELTAS_MICRO: [u32; 7] = [6_250, 8_000, 12_500, 25_000, 50_000, 100_000, 200_000];
const SUPERFLUX_FLOORS_MICRO: [u32; 6] = [0, 25_000, 50_000, 100_000, 200_000, 400_000];
const MEL32_DELTAS_MICRO: [u32; 6] = [250_000, 500_000, 750_000, 1_000_000, 1_500_000, 2_000_000];
const MEL32_FLOORS_MICRO: [u32; 6] = [0, 500_000, 1_000_000, 1_500_000, 2_000_000, 3_000_000];

#[derive(Clone, Copy, Debug, PartialEq, Deserialize, Serialize)]
#[serde(tag = "mode", rename_all = "snake_case", deny_unknown_fields)]
pub(crate) enum PeakRule {
    LegacyAbsolute {
        threshold: f32,
        radius_hops: usize,
        refractory_ms: f64,
    },
    LocalMean {
        delta: f32,
        absolute_floor: f32,
        pre_max_hops: usize,
        post_max_hops: usize,
        pre_avg_hops: usize,
        post_avg_hops: usize,
        refractory_ms: f64,
    },
}

impl PeakRule {
    pub(crate) fn refractory_samples(self, sample_rate: u32) -> i64 {
        match self {
            Self::LegacyAbsolute { refractory_ms, .. } | Self::LocalMean { refractory_ms, .. } => {
                (refractory_ms * f64::from(sample_rate) / 1_000.0).round() as i64
            }
        }
    }

    fn validate_diagnostic(self) -> Result<(), String> {
        match self {
            Self::LegacyAbsolute {
                threshold,
                radius_hops,
                refractory_ms,
            } => {
                if !threshold.is_finite()
                    || threshold < 0.0
                    || radius_hops == 0
                    || !valid_positive(refractory_ms)
                {
                    return Err("invalid legacy peak rule".to_string());
                }
            }
            Self::LocalMean {
                delta,
                absolute_floor,
                pre_max_hops,
                post_max_hops: _,
                pre_avg_hops,
                post_avg_hops: _,
                refractory_ms,
            } => {
                if !delta.is_finite()
                    || !absolute_floor.is_finite()
                    || delta < 0.0
                    || absolute_floor < 0.0
                    || pre_max_hops == 0
                    || pre_avg_hops == 0
                    || (refractory_ms - 30.0).abs() > f64::EPSILON
                {
                    return Err("invalid v2 local-mean peak rule".to_string());
                }
            }
        }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize, Serialize)]
#[serde(tag = "mode", rename_all = "snake_case", deny_unknown_fields)]
pub(crate) enum FormalPeakRule {
    LocalMean {
        delta_micro_units: u32,
        absolute_floor_micro_units: u32,
        pre_max_hops: u8,
        post_max_hops: u8,
        pre_avg_hops: u8,
        post_avg_hops: u8,
        refractory_micros: u32,
    },
}

impl FormalPeakRule {
    fn validate(self, analyzer: FormalAnalyzerConfig) -> Result<(), String> {
        let Self::LocalMean {
            delta_micro_units,
            absolute_floor_micro_units,
            pre_max_hops,
            post_max_hops,
            pre_avg_hops,
            post_avg_hops,
            refractory_micros,
        } = self;
        let (deltas, floors): (&[u32], &[u32]) = match analyzer {
            FormalAnalyzerConfig::Mel32V2 => (&MEL32_DELTAS_MICRO, &MEL32_FLOORS_MICRO),
            FormalAnalyzerConfig::FixedSuperflux { .. } => {
                (&SUPERFLUX_DELTAS_MICRO, &SUPERFLUX_FLOORS_MICRO)
            }
        };
        if !deltas.contains(&delta_micro_units)
            || !floors.contains(&absolute_floor_micro_units)
            || !matches!(pre_max_hops, 3 | 6)
            || !matches!(post_max_hops, 0..=2)
            || post_avg_hops != post_max_hops
            || !matches!(pre_avg_hops, 12 | 19 | 24)
            || refractory_micros != 30_000
        {
            return Err("formal peak rule is outside the preregistered integer grid".to_string());
        }
        Ok(())
    }

    pub(crate) fn realize(self) -> PeakRule {
        let Self::LocalMean {
            delta_micro_units,
            absolute_floor_micro_units,
            pre_max_hops,
            post_max_hops,
            pre_avg_hops,
            post_avg_hops,
            refractory_micros,
        } = self;
        PeakRule::LocalMean {
            delta: delta_micro_units as f32 / 1_000_000.0,
            absolute_floor: absolute_floor_micro_units as f32 / 1_000_000.0,
            pre_max_hops: usize::from(pre_max_hops),
            post_max_hops: usize::from(post_max_hops),
            pre_avg_hops: usize::from(pre_avg_hops),
            post_avg_hops: usize::from(post_avg_hops),
            refractory_ms: f64::from(refractory_micros) / 1_000.0,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct DiagnosticAnalyzerConfig {
    pub(crate) odf_kind: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Deserialize, Serialize)]
#[serde(tag = "front_end", rename_all = "snake_case", deny_unknown_fields)]
pub(crate) enum FormalAnalyzerConfig {
    Mel32V2,
    FixedSuperflux {
        reference_window_samples: u32,
        bands_per_octave: u32,
        maximum_filter_radius: usize,
        reference_dbfs: i32,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum FormalAnalyzer {
    Mel32V2,
    FixedSuperflux(SuperFluxConfig),
}

impl FormalAnalyzerConfig {
    fn validate(self) -> Result<(), String> {
        match self {
            Self::Mel32V2 => Ok(()),
            Self::FixedSuperflux {
                reference_window_samples,
                bands_per_octave,
                maximum_filter_radius,
                reference_dbfs,
            } if matches!(reference_window_samples, 1_024 | 2_048)
                && matches!(bands_per_octave, 12 | 24)
                && maximum_filter_radius <= 1
                && matches!(reference_dbfs, -80 | -70 | -60 | -50) =>
            {
                Ok(())
            }
            Self::FixedSuperflux { .. } => {
                Err("formal SuperFlux front end is outside the preregistered grid".to_string())
            }
        }
    }

    pub(crate) fn realize(self) -> FormalAnalyzer {
        match self {
            Self::Mel32V2 => FormalAnalyzer::Mel32V2,
            Self::FixedSuperflux {
                reference_window_samples,
                bands_per_octave,
                maximum_filter_radius,
                reference_dbfs,
            } => FormalAnalyzer::FixedSuperflux(SuperFluxConfig::new(
                reference_window_samples,
                bands_per_octave,
                maximum_filter_radius,
                reference_dbfs,
                SuperFluxChannelMode::Lr,
                1,
            )),
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(tag = "schema", deny_unknown_fields)]
pub(crate) enum CandidateConfig {
    #[serde(rename = "kirin-hypha-attack-candidate-config-v1")]
    Diagnostic {
        candidate_id: String,
        profile: String,
        eligibility: String,
        analyzer: DiagnosticAnalyzerConfig,
        peak_picker: PeakRule,
    },
    #[serde(rename = "kirin-hypha-attack-drum-formal-candidate-config-v1")]
    FormalDrum {
        candidate_id: String,
        profile: String,
        eligibility: String,
        analyzer: FormalAnalyzerConfig,
        peak_picker: FormalPeakRule,
    },
}

impl CandidateConfig {
    pub(crate) fn read(path: &Path) -> Result<CandidateArtifact, String> {
        let raw = fs::read(path)
            .map_err(|error| format!("cannot read candidate config {}: {error}", path.display()))?;
        let config: Self = serde_json::from_slice(&raw)
            .map_err(|error| format!("invalid candidate config JSON: {error}"))?;
        config.validate()?;
        let semantic = serde_json::to_vec(&config)
            .map_err(|error| format!("cannot canonicalize candidate config: {error}"))?;
        Ok(CandidateArtifact {
            config,
            raw_sha256: sha256_bytes(&raw),
            semantic_sha256: sha256_bytes(&semantic),
        })
    }

    pub(crate) fn candidate_id(&self) -> &str {
        match self {
            Self::Diagnostic { candidate_id, .. } | Self::FormalDrum { candidate_id, .. } => {
                candidate_id
            }
        }
    }

    pub(crate) fn diagnostic_parts(&self) -> Result<(&DiagnosticAnalyzerConfig, PeakRule), String> {
        match self {
            Self::Diagnostic {
                analyzer,
                peak_picker,
                ..
            } => Ok((analyzer, *peak_picker)),
            Self::FormalDrum { .. } => {
                Err("formal candidate config cannot be used for opened diagnostics".to_string())
            }
        }
    }

    pub(crate) fn diagnostic_kind(&self) -> Result<TransientOdfKind, String> {
        let (analyzer, _) = self.diagnostic_parts()?;
        match analyzer.odf_kind.as_str() {
            "mel32" => Ok(TransientOdfKind::Mel32),
            "mel40" => Ok(TransientOdfKind::Mel40),
            "complex" => Ok(TransientOdfKind::Complex),
            "hybrid" => Ok(TransientOdfKind::Hybrid),
            _ => Err("unsupported candidate ODF kind".to_string()),
        }
    }

    pub(crate) fn formal_parts(
        &self,
    ) -> Result<(FormalAnalyzerConfig, FormalAnalyzer, PeakRule), String> {
        match self {
            Self::FormalDrum {
                analyzer,
                peak_picker,
                ..
            } => Ok((*analyzer, analyzer.realize(), peak_picker.realize())),
            Self::Diagnostic { .. } => {
                Err("diagnostic candidate config cannot enter formal evaluation".to_string())
            }
        }
    }

    fn validate(&self) -> Result<(), String> {
        match self {
            Self::Diagnostic {
                candidate_id,
                profile,
                eligibility,
                peak_picker,
                ..
            } => {
                if candidate_id.is_empty() || profile != "DRUM" {
                    return Err("candidate ID/profile is invalid".to_string());
                }
                if eligibility != "diagnostic_only" {
                    return Err("diagnostic schema accepts only diagnostic_only".to_string());
                }
                self.diagnostic_kind()?;
                peak_picker.validate_diagnostic()
            }
            Self::FormalDrum {
                candidate_id,
                profile,
                eligibility,
                analyzer,
                peak_picker,
            } => {
                if candidate_id.is_empty() || profile != "DRUM" {
                    return Err("formal candidate ID/profile is invalid".to_string());
                }
                if eligibility != "formal_development_candidate" {
                    return Err(
                        "formal schema accepts only formal_development_candidate".to_string()
                    );
                }
                analyzer.validate()?;
                peak_picker.validate(*analyzer)
            }
        }
    }
}

#[derive(Debug)]
pub(crate) struct CandidateArtifact {
    pub(crate) config: CandidateConfig,
    pub(crate) raw_sha256: String,
    pub(crate) semantic_sha256: String,
}

fn valid_positive(value: f64) -> bool {
    value.is_finite() && value > 0.0
}
