//! Legacy PRE format identity, independent of its processing lifecycle.

use super::HyphaPre;
use nih_plug::prelude::{Vst3Plugin, Vst3SubCategory};

impl Vst3Plugin for HyphaPre {
    const VST3_CLASS_ID: [u8; 16] = *b"KirinHyphaPREv01";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] =
        &[Vst3SubCategory::Fx, Vst3SubCategory::Analyzer];
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pre_identity_and_categories_remain_compatible() {
        assert_eq!(HyphaPre::VST3_CLASS_ID, *b"KirinHyphaPREv01");
        assert!(matches!(
            HyphaPre::VST3_SUBCATEGORIES,
            [Vst3SubCategory::Fx, Vst3SubCategory::Analyzer]
        ));
    }
}
