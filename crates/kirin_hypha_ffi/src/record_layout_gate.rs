use super::{license_from_abi, KirinHyphaEngine, LICENSE_OS, LICENSE_SENSE, LICENSE_UNKNOWN};
use kirin_measure::channel_layout::LayoutId;

const MEASUREMENT_ONLY_NOTICE: &str = "Record is available for mono / stereo only";

impl KirinHyphaEngine {
    /// Sets the entitlement used by the next Keep without stopping an already-started Keep.
    /// 5.1 keeps this Unknown so an external PRE signal cannot bypass the measurement-only gate.
    pub fn set_license(&self, abi: u8) {
        self.license
            .store(license_from_abi(self.record_license_code(abi)));
    }

    pub(super) fn supports_record_workflow(&self) -> bool {
        matches!(self.layout.id(), LayoutId::Mono | LayoutId::Stereo)
    }

    pub(super) fn supports_optional_analysis(&self) -> bool {
        self.supports_record_workflow()
    }

    pub(super) fn record_license_code(&self, requested: u8) -> u8 {
        if !self.supports_record_workflow() {
            return LICENSE_UNKNOWN;
        }
        match requested {
            LICENSE_OS => LICENSE_OS,
            LICENSE_SENSE => LICENSE_SENSE,
            _ => LICENSE_UNKNOWN,
        }
    }

    pub(super) fn reject_unsupported_record_action(&self, user_initiated: bool) -> bool {
        if self.supports_record_workflow() {
            return false;
        }
        if user_initiated {
            if let Ok(mut notice) = self.keep_action_notice.write() {
                *notice = Some(MEASUREMENT_ONLY_NOTICE.to_string());
            }
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::super::{ChannelLayout, KirinHyphaEngine, LICENSE_OS};
    use kirin_measure::channel_layout::LayoutId;

    #[test]
    fn exact_five_one_stays_measurement_only_even_with_os_license() {
        let engine = KirinHyphaEngine::new(48_000, ChannelLayout::by_id(LayoutId::Surround5_1));
        engine.set_license(LICENSE_OS);

        assert!(!engine.enter_record());
        assert!(!engine.is_recording());
    }

    #[test]
    fn exact_five_one_rejects_every_optional_analysis_entry() {
        let engine = KirinHyphaEngine::new(48_000, ChannelLayout::by_id(LayoutId::Surround5_1));
        *engine.write_role.lock().unwrap() = Some(super::super::PluginDataRole::Post);
        assert!(!engine.set_spectrum_visible(true));
        assert!(!engine.set_perceptual_visible(true));
        assert!(!engine.set_absolute_visible(true));
        assert!(!engine.set_spectrum_channel_mode(0));
        assert!(!engine.set_attack_enabled(true));
        assert!(!engine.spectrum_stats().enabled);
        assert_eq!(engine.attack_stats().enabled, 0);
    }
}
