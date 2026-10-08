use super::*;

#[test]
fn canonical_descriptor_has_independently_packed_v1_golden_hash() {
    // Independently packed with Python struct '<I'/'<d' and hashlib SHA-256, not this encoder.
    let expected = [
        0xca, 0x24, 0x7a, 0xf1, 0x02, 0x6b, 0xb7, 0x18, 0x05, 0x01, 0x9f, 0x85, 0x91, 0xe2, 0x47,
        0xb8, 0xc6, 0xd7, 0x53, 0xff, 0x77, 0xd3, 0x96, 0xd6, 0x72, 0x59, 0xee, 0xd2, 0xca, 0x31,
        0x94, 0x90,
    ];
    let bytes = band_semantic_bytes();
    assert_eq!(BAND_SEMANTIC_VERSION, 1);
    assert_eq!(bytes.len(), 361);
    assert_eq!(&bytes[..23], b"KirinHypha/BandMeaning\0");
    assert_eq!(&bytes[23..27], &[1, 0, 0, 0]);
    assert_eq!(&bytes[47..51], &[0x20, 0x4e, 0, 0]); // 20,000 us HEAD lead.
    assert_eq!(&bytes[63..67], &[96, 0, 0, 0]);
    assert_eq!(&bytes[67..71], &[64, 0, 0, 0]);
    assert_eq!(band_semantic_hash(), expected);
    assert_eq!(band_semantic_bytes(), bytes);
}

#[test]
fn changing_one_semantic_byte_changes_the_golden_meaning() {
    let mut bytes = band_semantic_bytes();
    bytes[47] ^= 1;
    let mutated: [u8; 32] = Sha256::digest(bytes).into();
    assert_ne!(mutated, band_semantic_hash());
}
