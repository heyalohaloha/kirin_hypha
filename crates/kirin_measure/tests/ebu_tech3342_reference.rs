//! Local-only EBU Tech 3342 verification.
//!
//! The EBU archive is licensed test material and must stay outside this repository. Run this
//! ignored gate with `KIRIN_EBU_TEST_SET_ZIP=/absolute/path/to/archive.zip`.

use std::fs::File;
use std::io;
use std::path::Path;

use ebur128::{Channel, EbuR128, Mode};
use kirin_measure::MeasureEngine;
use symphonia::core::audio::SampleBuffer;
use symphonia::core::codecs::{DecoderOptions, CODEC_TYPE_NULL};
use symphonia::core::formats::FormatOptions;
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;
use symphonia::core::probe::Hint;
use zip::ZipArchive;

const CASES: &[(&str, f64)] = &[
    ("seq-3342-1-16bit.wav", 10.0),
    ("seq-3342-2-16bit.wav", 5.0),
    ("seq-3342-3-16bit.wav", 20.0),
    ("seq-3342-4-16bit.wav", 15.0),
    ("seq-3341-7_seq-3342-5-24bit.wav", 5.0),
    ("seq-3341-2011-8_seq-3342-6-24bit-v02.wav", 15.0),
];

fn extract_case(archive_path: &Path, name: &str) -> tempfile::NamedTempFile {
    let archive_file = File::open(archive_path).expect("open EBU archive");
    let mut archive = ZipArchive::new(archive_file).expect("read EBU archive");
    let mut source = archive.by_name(name).expect("required Tech 3342 WAV");
    let mut destination = tempfile::NamedTempFile::new().expect("create private test file");
    io::copy(&mut source, &mut destination).expect("extract private test file");
    destination
}

fn measure(path: &Path) -> (f64, f64) {
    let file = File::open(path).expect("open extracted WAV");
    let stream = MediaSourceStream::new(Box::new(file), Default::default());
    let mut hint = Hint::new();
    hint.with_extension("wav");
    let probed = symphonia::default::get_probe()
        .format(
            &hint,
            stream,
            &FormatOptions::default(),
            &MetadataOptions::default(),
        )
        .expect("probe WAV");
    let mut format = probed.format;
    let track = format
        .tracks()
        .iter()
        .find(|track| track.codec_params.codec != CODEC_TYPE_NULL)
        .expect("audio track");
    let track_id = track.id;
    let sample_rate = track.codec_params.sample_rate.expect("sample rate");
    let channels = track.codec_params.channels.expect("channels").count();
    assert_eq!(sample_rate, 48_000);
    assert_eq!(channels, 2);
    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())
        .expect("PCM decoder");
    let mut reference =
        EbuR128::new(channels as u32, sample_rate, Mode::LRA).expect("reference analyzer");
    reference
        .set_channel_map(&[Channel::Left, Channel::Right])
        .expect("reference channel map");
    let mut hypha = MeasureEngine::new(sample_rate, channels).expect("Hypha engine");
    loop {
        use symphonia::core::errors::Error as SymphoniaError;
        let packet = match format.next_packet() {
            Ok(packet) => packet,
            Err(SymphoniaError::IoError(error)) if error.kind() == io::ErrorKind::UnexpectedEof => {
                break;
            }
            Err(error) => panic!("read WAV packet: {error}"),
        };
        if packet.track_id() != track_id {
            continue;
        }
        let decoded = decoder.decode(&packet).expect("decode PCM packet");
        let mut samples = SampleBuffer::<f64>::new(decoded.capacity() as u64, *decoded.spec());
        samples.copy_interleaved_ref(decoded);
        reference
            .add_frames_f64(samples.samples())
            .expect("feed reference analyzer");
        hypha.push(samples.samples());
    }
    (
        reference.loudness_range().expect("reference LRA"),
        hypha.finalize().lra.expect("Hypha LRA"),
    )
}

#[test]
#[ignore = "requires separately supplied EBU test archive"]
fn tech3342_lra_matches_reference_and_hypha_integration() {
    let archive = std::env::var_os("KIRIN_EBU_TEST_SET_ZIP")
        .map(std::path::PathBuf::from)
        .expect("set KIRIN_EBU_TEST_SET_ZIP to the local EBU v05 archive");
    for &(name, expected) in CASES {
        let extracted = extract_case(&archive, name);
        let (reference, hypha) = measure(extracted.path());
        assert!(
            (reference - expected).abs() <= 1.0,
            "{name}: reference={reference}"
        );
        assert!((hypha - expected).abs() <= 1.0, "{name}: Hypha={hypha}");
        assert!(
            (hypha - reference).abs() <= 0.1,
            "{name}: {hypha} vs {reference}"
        );
    }
}
