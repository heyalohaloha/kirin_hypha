//! Shared optional measurement fields for live PRE/POST JSON.

use crate::MeasureResult;

pub(crate) fn phase_d_fragment(result: &MeasureResult) -> String {
    let mut fields = String::new();
    if let Some(n) = result.n_prime_total {
        fields.push_str(&format!(r#","n_prime_total":{:.3}"#, n));
    }
    if let Some(sharpness) = result.sharpness {
        fields.push_str(&format!(r#","sharpness":{:.3}"#, sharpness));
    }
    if let Some(ref psb) = result.psb_summary {
        fields.push_str(&format!(
            r#","psb_summary":{{"low":{:.3},"mid":{:.3},"high":{:.3}}}"#,
            psb.low, psb.mid, psb.high
        ));
    }
    if let Some(psb) = result.psb_bark {
        if let Ok(json) = serde_json::to_string(&psb) {
            fields.push_str(&format!(r#","psb_bark":{json}"#));
        }
    }
    fields
}
