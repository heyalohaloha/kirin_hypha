//! Stable and semantic channel identity for a captured PRE/POST pair.
//!
//! `channel_key` is the persisted PRE instance id. It identifies one exact DAW channel without
//! depending on a user-editable name and therefore survives DAW save/reopen. `channel_role` is
//! separate, optional metadata derived only from a small exact alias set. Unknown names remain
//! valid and visible in TRACE; they simply do not participate in role-specific automatic linking.

/// Return the canonical Kirin channel role for an exact, unambiguous display-name alias.
pub fn canonical_channel_role(display_name: &str) -> Option<&'static str> {
    match normalized_alias(display_name).as_str() {
        "2mix" | "mix" | "mixbus" | "master" | "masterbus" | "main" | "stereoout" | "メイン"
        | "ミックス" | "マスター" => Some("bus_mix"),
        "drum" | "drums" | "drumbus" | "ドラム" => Some("bus_drum"),
        "bass" | "bassbus" | "ベース" => Some("bus_bass"),
        "vocal" | "vocals" | "vocalbus" | "vox" | "ボーカル" => Some("bus_vocal"),
        "bv" | "bvox" | "backingvocal" | "backingvocals" | "コーラス" => Some("bus_bv"),
        "music" | "musicbus" | "instrumental" | "inst" | "ミュージック" | "音楽" | "オケ" => {
            Some("bus_music")
        }
        "space" | "spacebus" | "fxbus" | "スペース" => Some("bus_space"),
        "room" | "roomaux" | "ルーム" => Some("aux_room"),
        "plate" | "plateaux" | "プレート" => Some("aux_plate"),
        "chamber" | "chamberaux" | "チェンバー" => Some("aux_chamber"),
        "hall" | "hallaux" | "ホール" => Some("aux_hall"),
        "delay" | "delayaux" | "ディレイ" => Some("aux_delay"),
        _ => None,
    }
}

/// Preserve the user's spelling while removing surrounding whitespace and empty metadata.
pub fn display_name_snapshot(display_name: &str) -> Option<String> {
    let trimmed = display_name.trim();
    (!trimmed.is_empty()).then(|| trimmed.to_string())
}

fn normalized_alias(value: &str) -> String {
    value
        .trim()
        .chars()
        .flat_map(char::to_lowercase)
        .filter(|ch| ch.is_alphanumeric())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_exact_aliases_have_semantic_roles() {
        assert_eq!(canonical_channel_role("2 Mix"), Some("bus_mix"));
        assert_eq!(canonical_channel_role("Drum Bus"), Some("bus_drum"));
        assert_eq!(canonical_channel_role("Music"), Some("bus_music"));
        assert_eq!(canonical_channel_role("ボーカル"), Some("bus_vocal"));
    }

    #[test]
    fn arbitrary_user_names_remain_valid_without_a_key() {
        assert_eq!(canonical_channel_role("Blue Mycelium 07"), None);
        assert_eq!(
            display_name_snapshot("  Blue Mycelium 07  ").as_deref(),
            Some("Blue Mycelium 07")
        );
    }
}
