//! One-time migration ABI for state written by the shipped nih-plug VST3.

use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use serde_json::Value;

use super::{write_c_buf, ID_BUF_LEN};

/// Fixed-size DTO used to move legacy nih-plug state into the JUCE shell.
/// State restoration runs on the message thread and never reaches the Audio Thread.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct KirinLegacyNihState {
    pub instance_id: [c_char; ID_BUF_LEN],
    pub project_uuid: [c_char; ID_BUF_LEN],
    pub daw_session_uuid: [c_char; ID_BUF_LEN],
    pub name: [c_char; ID_BUF_LEN],
    pub pair_pre_name: [c_char; ID_BUF_LEN],
}

impl Default for KirinLegacyNihState {
    fn default() -> Self {
        Self {
            instance_id: [0; ID_BUF_LEN],
            project_uuid: [0; ID_BUF_LEN],
            daw_session_uuid: [0; ID_BUF_LEN],
            name: [0; ID_BUF_LEN],
            pair_pre_name: [0; ID_BUF_LEN],
        }
    }
}

fn legacy_nih_field(fields: &serde_json::Map<String, Value>, key: &str) -> Option<String> {
    let encoded = fields.get(key)?.as_str()?;
    serde_json::from_str::<String>(encoded).ok()
}

fn decode_legacy_nih_state_bytes(data: &[u8]) -> Option<KirinLegacyNihState> {
    // Old shipped nih-plug did not enable its optional zstd feature. Keep this decoder deliberately
    // narrow: accepting arbitrary compressed/enveloped data would turn state restore into a second
    // format-discovery system. JUCE XML remains the sole current writer.
    const MAX_LEGACY_STATE_BYTES: usize = 1024 * 1024;
    if data.is_empty() || data.len() > MAX_LEGACY_STATE_BYTES {
        return None;
    }
    let root: Value = serde_json::from_slice(data).ok()?;
    let fields = root.get("fields")?.as_object()?;
    let instance_id = legacy_nih_field(fields, "instance_id");
    let project_uuid = legacy_nih_field(fields, "project_uuid");
    let daw_session_uuid = legacy_nih_field(fields, "daw_session_uuid");
    let name = legacy_nih_field(fields, "name");
    let pair_pre_name = legacy_nih_field(fields, "pair_pre_name");
    if instance_id.is_none()
        && project_uuid.is_none()
        && daw_session_uuid.is_none()
        && name.is_none()
        && pair_pre_name.is_none()
    {
        return None;
    }

    let mut out = KirinLegacyNihState::default();
    write_c_buf(
        &mut out.instance_id,
        instance_id.as_deref().unwrap_or_default(),
    );
    write_c_buf(
        &mut out.project_uuid,
        project_uuid.as_deref().unwrap_or_default(),
    );
    write_c_buf(
        &mut out.daw_session_uuid,
        daw_session_uuid.as_deref().unwrap_or_default(),
    );
    write_c_buf(&mut out.name, name.as_deref().unwrap_or_default());
    write_c_buf(
        &mut out.pair_pre_name,
        pair_pre_name.as_deref().unwrap_or_default(),
    );
    Some(out)
}

/// Shipped nih-plug VST3 state -> JUCE common-shell one-time migration.
/// This is invoked only from the host's state restore callback, never from `processBlock`.
///
/// # Safety
/// `data` must reference `len` readable bytes and `out` must reference writable storage.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_decode_legacy_nih_state(
    data: *const u8,
    len: usize,
    out: *mut KirinLegacyNihState,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if data.is_null() || out.is_null() {
            return false;
        }
        let bytes = unsafe { std::slice::from_raw_parts(data, len) };
        let Some(decoded) = decode_legacy_nih_state_bytes(bytes) else {
            return false;
        };
        unsafe { *out = decoded };
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::{decode_legacy_nih_state_bytes, KirinLegacyNihState, ID_BUF_LEN};
    use std::ffi::CStr;
    use std::os::raw::c_char;

    fn field(state: &KirinLegacyNihState, which: &str) -> String {
        let ptr = match which {
            "instance_id" => state.instance_id.as_ptr(),
            "project_uuid" => state.project_uuid.as_ptr(),
            "daw_session_uuid" => state.daw_session_uuid.as_ptr(),
            "name" => state.name.as_ptr(),
            "pair_pre_name" => state.pair_pre_name.as_ptr(),
            _ => unreachable!(),
        };
        unsafe { CStr::from_ptr(ptr) }
            .to_string_lossy()
            .into_owned()
    }

    #[test]
    fn c_layout_remains_five_fixed_identity_buffers() {
        assert_eq!(
            std::mem::size_of::<KirinLegacyNihState>(),
            5 * ID_BUF_LEN * std::mem::size_of::<c_char>()
        );
        assert_eq!(
            std::mem::align_of::<KirinLegacyNihState>(),
            std::mem::align_of::<c_char>()
        );
    }

    #[test]
    fn decodes_pre_identity_from_exact_nih_fields_contract() {
        let bytes = br#"{"version":"1.1.26","params":{},"fields":{"instance_id":"\"iid-pre\"","project_uuid":"\"project-a\"","daw_session_uuid":"\"session-a\"","name":"\"Drum\""}}"#;
        let state = decode_legacy_nih_state_bytes(bytes).expect("legacy PRE state");
        assert_eq!(field(&state, "instance_id"), "iid-pre");
        assert_eq!(field(&state, "project_uuid"), "project-a");
        assert_eq!(field(&state, "daw_session_uuid"), "session-a");
        assert_eq!(field(&state, "name"), "Drum");
        assert_eq!(field(&state, "pair_pre_name"), "");
    }

    #[test]
    fn decodes_post_pair_and_rejects_unrelated_or_malformed_state() {
        let bytes = br#"{"version":"1.1.26","params":{"bypass":{"Bool":false}},"fields":{"instance_id":"\"iid-post\"","project_uuid":"\"project-a\"","daw_session_uuid":"\"session-a\"","pair_pre_name":"\"2Mix\"","pair_claimed_at":"12.0"}}"#;
        let state = decode_legacy_nih_state_bytes(bytes).expect("legacy POST state");
        assert_eq!(field(&state, "instance_id"), "iid-post");
        assert_eq!(field(&state, "pair_pre_name"), "2Mix");
        assert!(decode_legacy_nih_state_bytes(br#"{"fields":{"other":"\"x\""}}"#).is_none());
        assert!(decode_legacy_nih_state_bytes(b"not-json").is_none());
        assert!(decode_legacy_nih_state_bytes(&vec![b' '; 1024 * 1024 + 1]).is_none());
    }
}
