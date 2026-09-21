//! Legacy POST format identity, independent of its processing lifecycle.

use super::HyphaPost;
use nih_plug::prelude::{Vst3Plugin, Vst3SubCategory};

impl Vst3Plugin for HyphaPost {
    const VST3_CLASS_ID: [u8; 16] = *b"KirinHyphaPOSTv1";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] =
        &[Vst3SubCategory::Fx, Vst3SubCategory::Analyzer];
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn post_identity_and_categories_remain_compatible() {
        assert_eq!(HyphaPost::VST3_CLASS_ID, *b"KirinHyphaPOSTv1");
        assert!(matches!(
            HyphaPost::VST3_SUBCATEGORIES,
            [Vst3SubCategory::Fx, Vst3SubCategory::Analyzer]
        ));
    }
}
