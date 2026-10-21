use super::*;

#[test]
fn canonical_descriptor_has_independently_packed_v2_golden_hash() {
    // Independently packed with Python struct '<I'/'<d' and hashlib SHA-256, not this encoder.
    let expected = [
        0x36, 0xc8, 0x38, 0x52, 0x3e, 0x5e, 0x87, 0x76, 0xbb, 0x88, 0x6b, 0x14, 0x0e, 0x7e, 0xad,
        0x15, 0xc4, 0xff, 0x23, 0x3d, 0x33, 0xc9, 0x2f, 0xf1, 0xb9, 0x2b, 0x7f, 0xda, 0x7b, 0x07,
        0x9d, 0x6b,
    ];
    let bytes = band_semantic_bytes();
    assert_eq!(BAND_SEMANTIC_VERSION, 2);
    assert_eq!(bytes.len(), 378);
    assert_eq!(&bytes[..23], b"KirinHypha/BandMeaning\0");
    assert_eq!(&bytes[23..27], &[2, 0, 0, 0]);
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
