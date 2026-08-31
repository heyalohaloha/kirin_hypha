use std::collections::BTreeSet;
use std::fs::File;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

use ebur128::{EbuR128, Mode};
use hound::{SampleFormat, WavReader};
use kirin_measure::{MeasureEngine, SessionSummary};
use sha2::{Digest, Sha256};
use tempfile::NamedTempFile;
use zip::ZipArchive;

pub const ARCHIVE_SHA256: &str = "9cc500b4df83f7c21855c74dce795ef5209a752bf884253ae57d0ce512efb062";

#[derive(Clone, Copy, Debug)]
pub struct Point {
    pub frames: u64,
    pub lufs_m: Option<f64>,
    pub lufs_s: Option<f64>,
    pub max_lufs_m: Option<f64>,
    pub max_lufs_s: Option<f64>,
}

#[derive(Debug)]
pub struct Measurement {
    pub name: String,
    pub sample_rate: u32,
    pub channels: usize,
    pub source_frames: u64,
    pub observed_frames: u64,
    pub points: Vec<Point>,
    pub hypha: SessionSummary,
    pub reference_i: Option<f64>,
    pub reference_lra: Option<f64>,
    pub reference_max_true_peak: Option<f64>,
}

impl Measurement {
    pub fn max_m(&self) -> Option<f64> {
        self.points.last().and_then(|point| point.max_lufs_m)
    }

    pub fn max_s(&self) -> Option<f64> {
        self.points.last().and_then(|point| point.max_lufs_s)
    }

    pub fn last_m(&self) -> Option<f64> {
        self.points.last().and_then(|point| point.lufs_m)
    }

    pub fn last_s(&self) -> Option<f64> {
        self.points.last().and_then(|point| point.lufs_s)
    }

    pub fn segment_max_s(&self, segment: usize) -> Option<f64> {
        self.points
            .get((segment + 1) * 60 - 1)
            .and_then(|point| point.max_lufs_s)
    }

    pub fn segment_max_m(&self, segment: usize) -> Option<f64> {
        self.points
            .get((segment + 1) * 8 - 1)
            .and_then(|point| point.max_lufs_m)
    }
}

pub fn archive_path() -> PathBuf {
    std::env::var_os("KIRIN_EBU_TEST_SET_ZIP")
        .map(PathBuf::from)
        .expect("set KIRIN_EBU_TEST_SET_ZIP to the local EBU Loudness Test Set v05 archive")
}

pub fn archive_sha256(path: &Path) -> String {
    let mut file = File::open(path).expect("open EBU archive for SHA-256");
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file.read(&mut buffer).expect("hash EBU archive");
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    format!("{:x}", digest.finalize())
}

pub fn archive_wav_names(path: &Path) -> BTreeSet<String> {
    let file = File::open(path).expect("open EBU archive");
    let mut archive = ZipArchive::new(file).expect("read EBU archive");
    (0..archive.len())
        .filter_map(|index| {
            let entry = archive.by_index(index).expect("read EBU archive entry");
            entry
                .name()
                .to_ascii_lowercase()
                .ends_with(".wav")
                .then(|| entry.name().to_owned())
        })
        .collect()
}

pub fn expected_wav_names() -> BTreeSet<String> {
    let mut names = BTreeSet::from([
        "1kHz Sine -20 LUFS-16bit.wav".to_owned(),
        "1kHz Sine -26 LUFS-16bit.wav".to_owned(),
        "1kHz Sine -40 LUFS-16bit.wav".to_owned(),
        "EBU-reference_listening_signal_pinknoise_500Hz_2kHz_R128.wav".to_owned(),
        "seq-3341-1-16bit.wav".to_owned(),
        "seq-3341-2-16bit.wav".to_owned(),
        "seq-3341-3-16bit-v02.wav".to_owned(),
        "seq-3341-4-16bit-v02.wav".to_owned(),
        "seq-3341-5-16bit-v02.wav".to_owned(),
        "seq-3341-6-5channels-16bit.wav".to_owned(),
        "seq-3341-6-6channels-WAVEEX-16bit.wav".to_owned(),
        "seq-3341-7_seq-3342-5-24bit.wav".to_owned(),
        "seq-3341-2011-8_seq-3342-6-24bit-v02.wav".to_owned(),
        "seq-3341-9-24bit.wav".to_owned(),
        "seq-3341-11-24bit.wav".to_owned(),
        "seq-3341-12-24bit.wav".to_owned(),
        "seq-3341-14-24bit.wav.wav".to_owned(),
        "seq-3342-1-16bit.wav".to_owned(),
        "seq-3342-2-16bit.wav".to_owned(),
        "seq-3342-3-16bit.wav".to_owned(),
        "seq-3342-4-16bit.wav".to_owned(),
    ]);
    for index in 1..=20 {
        names.insert(format!("seq-3341-10-{index}-24bit.wav"));
        names.insert(format!(
            "seq-3341-13-{index}-24bit.wav{}",
            if index > 2 { ".wav" } else { "" }
        ));
    }
    for index in 15..=23 {
        names.insert(format!("seq-3341-{index}-24bit.wav.wav"));
    }
    names
}

fn extract(path: &Path, name: &str) -> NamedTempFile {
    let file = File::open(path).expect("open EBU archive");
    let mut archive = ZipArchive::new(file).expect("read EBU archive");
    let mut source = archive.by_name(name).expect("required EBU WAV");
    let mut destination = NamedTempFile::new().expect("create private EBU test file");
    io::copy(&mut source, &mut destination).expect("extract private EBU test file");
    destination
}

fn decode(path: &Path) -> (u32, usize, Vec<f64>) {
    // Hound is deliberate here: this is the same decoder used by ebur128 0.1.10's official
    // reference_tests.rs and accepts the EBU set's legacy 6-channel WAVE_FORMAT_EXTENSIBLE file.
    let reader = WavReader::open(path).expect("open extracted EBU WAV");
    let spec = reader.spec();
    assert_eq!(spec.sample_format, SampleFormat::Int, "EBU PCM format");
    let denominator = (1_u64 << (spec.bits_per_sample - 1)) as f64;
    let samples = if spec.bits_per_sample <= 16 {
        reader
            .into_samples::<i16>()
            .map(|sample| sample.expect("decode EBU 16-bit PCM") as f64 / denominator)
            .collect()
    } else {
        reader
            .into_samples::<i32>()
            .map(|sample| sample.expect("decode EBU 24-bit PCM") as f64 / denominator)
            .collect()
    };
    (spec.sample_rate, spec.channels as usize, samples)
}

pub fn measure(archive: &Path, name: &str) -> Measurement {
    let extracted = extract(archive, name);
    let (sample_rate, channels, samples) = decode(extracted.path());
    assert_eq!(sample_rate, 48_000, "{name}: sample rate");
    assert_eq!(samples.len() % channels, 0, "{name}: complete frames");

    let mode = Mode::M | Mode::S | Mode::I | Mode::LRA | Mode::TRUE_PEAK;
    let mut reference =
        EbuR128::new(channels as u32, sample_rate, mode).expect("create direct ebur128 reference");
    let mut hypha = MeasureEngine::new(sample_rate, channels).expect("create Hypha engine");
    let chunk_samples = sample_rate as usize / 10 * channels;
    let mut points = Vec::new();
    for chunk in samples.chunks(chunk_samples) {
        reference
            .add_frames_f64(chunk)
            .expect("feed direct ebur128 reference");
        if let Some(result) = hypha.push(chunk) {
            points.push(Point {
                frames: hypha.total_frames(),
                lufs_m: result.lufs_m,
                lufs_s: result.lufs_s,
                max_lufs_m: hypha.max_lufs_m(),
                max_lufs_s: hypha.max_lufs_s(),
            });
        }
    }

    let reference_max_true_peak = (0..channels as u32)
        .filter_map(|channel| reference.true_peak(channel).ok())
        .filter(|value| value.is_finite() && *value > 0.0)
        .fold(None, |maximum, value| {
            Some(maximum.map_or(value, |current: f64| current.max(value)))
        })
        .map(|linear| 20.0 * linear.log10());
    let source_frames = (samples.len() / channels) as u64;
    let observed_frames = hypha.total_frames();
    let hypha_summary = hypha.finalize();

    Measurement {
        name: name.to_owned(),
        sample_rate,
        channels,
        source_frames,
        observed_frames,
        points,
        hypha: hypha_summary,
        reference_i: reference
            .loudness_global()
            .ok()
            .filter(|value| value.is_finite()),
        reference_lra: reference
            .loudness_range()
            .ok()
            .filter(|value| value.is_finite()),
        reference_max_true_peak,
    }
}

pub fn fmt(value: Option<f64>) -> String {
    value.map_or_else(|| "---".to_owned(), |value| format!("{value:.3}"))
}
