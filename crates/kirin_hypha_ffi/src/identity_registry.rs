//! Role-scoped identity convergence for PRE/POST plugin instances.
//!
//! Empty/legacy `daw_session_uuid` values remain empty at runtime so the host-process legacy
//! bridge can resolve them. Saved DAW documents with a non-empty session UUID instead receive a
//! role-local identity group, preventing a later document in the same DAW process from adopting
//! the first document's project shelf.
//!
//! PRE and POST deliberately use separate registries. Production links them as separate dynamic
//! libraries, while the FFI parity tests load both roles into one process. Keeping the registries
//! role-scoped preserves the production boundary in those tests.

use std::collections::HashMap;
use std::sync::{Arc, Mutex, OnceLock, RwLock};

use uuid::Uuid;

type IdentityCellPair = (Arc<RwLock<String>>, Arc<RwLock<String>>);
type IdentityGroups = Mutex<HashMap<String, IdentityCellPair>>;

fn shared_pre_project_hash_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_pre_daw_session_id_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_post_project_hash_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_post_daw_session_id_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn shared_pre_identity_groups() -> &'static IdentityGroups {
    static GROUPS: OnceLock<IdentityGroups> = OnceLock::new();
    GROUPS.get_or_init(|| Mutex::new(HashMap::new()))
}

fn shared_post_identity_groups() -> &'static IdentityGroups {
    static GROUPS: OnceLock<IdentityGroups> = OnceLock::new();
    GROUPS.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Resolve one shared cell with first-wins semantics.
///
/// A non-empty restored candidate seeds an empty cell. An empty candidate generates one UUID and
/// seeds it. Once seeded, later candidates adopt the shared value without overwriting it.
fn resolve_shared_id(cell: &Arc<RwLock<String>>, candidate: &str) -> String {
    match cell.write() {
        Ok(mut value) => {
            if value.is_empty() {
                *value = if candidate.is_empty() {
                    Uuid::new_v4().to_string()
                } else {
                    candidate.to_string()
                };
            }
            value.clone()
        }
        // R-28 functional silence: preserve progress if the convergence cell is poisoned.
        Err(_) => {
            if candidate.is_empty() {
                Uuid::new_v4().to_string()
            } else {
                candidate.to_string()
            }
        }
    }
}

/// Read a live identity cell without propagating lock poisoning into the host.
pub(crate) fn read_shared_id(cell: &Arc<RwLock<String>>) -> String {
    cell.read().map(|value| value.clone()).unwrap_or_default()
}

fn resolve_role_identity(
    fallback_project_cell: &Arc<RwLock<String>>,
    _fallback_daw_cell: &Arc<RwLock<String>>,
    grouped_cells: &IdentityGroups,
    project_candidate: &str,
    daw_candidate: &str,
) -> (String, String) {
    if daw_candidate.is_empty() {
        return (
            resolve_shared_id(fallback_project_cell, project_candidate),
            String::new(),
        );
    }

    let (project_cell, daw_cell) = {
        let mut groups = grouped_cells
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        groups
            .entry(daw_candidate.to_string())
            .or_insert_with(|| {
                (
                    Arc::new(RwLock::new(String::new())),
                    Arc::new(RwLock::new(String::new())),
                )
            })
            .clone()
    };

    (
        resolve_shared_id(&project_cell, project_candidate),
        resolve_shared_id(&daw_cell, daw_candidate),
    )
}

pub(crate) fn resolve_pre_identity(
    project_candidate: &str,
    daw_candidate: &str,
) -> (String, String) {
    resolve_role_identity(
        shared_pre_project_hash_cell(),
        shared_pre_daw_session_id_cell(),
        shared_pre_identity_groups(),
        project_candidate,
        daw_candidate,
    )
}

pub(crate) fn resolve_post_identity(
    project_candidate: &str,
    daw_candidate: &str,
) -> (String, String) {
    resolve_role_identity(
        shared_post_project_hash_cell(),
        shared_post_daw_session_id_cell(),
        shared_post_identity_groups(),
        project_candidate,
        daw_candidate,
    )
}

pub(crate) fn store_resolved_identity_cells(
    project_cell: &Arc<RwLock<String>>,
    daw_cell: &Arc<RwLock<String>>,
    project_hash: &str,
    daw_session_id: &str,
) {
    if let Ok(mut value) = project_cell.write() {
        *value = project_hash.to_string();
    }
    if let Ok(mut value) = daw_cell.write() {
        *value = daw_session_id.to_string();
    }
}

/// Clear the PRE/POST fallback cells and saved-document groups without replacing their `Arc`s.
///
/// Keeping each cell handle stable preserves live reads in an existing IO thread. The engine calls
/// this only after its IO and measurement threads have joined; integration tests also call it to
/// isolate first-wins scenarios within one test process.
pub(crate) fn clear_role_scoped_cells() {
    for cell in [
        shared_pre_project_hash_cell(),
        shared_pre_daw_session_id_cell(),
        shared_post_project_hash_cell(),
        shared_post_daw_session_id_cell(),
    ] {
        if let Ok(mut value) = cell.write() {
            value.clear();
        }
    }
    if let Ok(mut groups) = shared_pre_identity_groups().lock() {
        groups.clear();
    }
    if let Ok(mut groups) = shared_post_identity_groups().lock() {
        groups.clear();
    }
}

/// Test-only integration hook for resetting role-scoped first-wins state.
#[doc(hidden)]
pub fn __reset_shared_ids_for_tests() {
    clear_role_scoped_cells();
}

#[cfg(test)]
mod tests {
    use super::{read_shared_id, resolve_role_identity, resolve_shared_id, IdentityGroups};
    use std::collections::HashMap;
    use std::sync::{Arc, Mutex, RwLock};

    #[test]
    fn second_instance_adopts_shared_value_never_overwrites() {
        let cell = Arc::new(RwLock::new(String::new()));
        let first = resolve_shared_id(&cell, "proj-A");
        let second = resolve_shared_id(&cell, "proj-B");
        assert_eq!(first, "proj-A");
        assert_eq!(second, "proj-A");
        assert_eq!(read_shared_id(&cell), "proj-A");
    }

    #[test]
    fn write_shelf_equals_all_live_scan_shelves() {
        let cell = Arc::new(RwLock::new(String::new()));
        let scan_clone_a = Arc::clone(&cell);
        let scan_clone_b = Arc::clone(&cell);
        let write_shelf = resolve_shared_id(&cell, "proj-1");
        let _ = resolve_shared_id(&cell, "proj-2");
        assert_eq!(write_shelf, "proj-1");
        assert_eq!(read_shared_id(&scan_clone_a), write_shelf);
        assert_eq!(read_shared_id(&scan_clone_b), write_shelf);
        assert_eq!(read_shared_id(&cell), write_shelf);
    }

    #[test]
    fn empty_candidate_generates_once_then_all_share() {
        let cell = Arc::new(RwLock::new(String::new()));
        let first = resolve_shared_id(&cell, "");
        assert!(!first.is_empty());
        let second = resolve_shared_id(&cell, "");
        assert_eq!(first, second);
        assert_eq!(read_shared_id(&Arc::clone(&cell)), first);
    }

    #[test]
    fn empty_cell_seeds_from_nonempty_chunk_candidate() {
        let cell = Arc::new(RwLock::new(String::new()));
        let resolved = resolve_shared_id(&cell, "restored-uuid");
        assert_eq!(resolved, "restored-uuid");
        assert_eq!(read_shared_id(&cell), "restored-uuid");
    }

    #[test]
    fn distinct_nonempty_daw_sessions_do_not_share_role_identity() {
        let fallback_project = Arc::new(RwLock::new(String::new()));
        let fallback_daw = Arc::new(RwLock::new(String::new()));
        let groups: IdentityGroups = Mutex::new(HashMap::new());

        let first = resolve_role_identity(
            &fallback_project,
            &fallback_daw,
            &groups,
            "project-mastering",
            "daw-mastering",
        );
        let second = resolve_role_identity(
            &fallback_project,
            &fallback_daw,
            &groups,
            "project-song",
            "daw-song",
        );

        assert_eq!(first, ("project-mastering".into(), "daw-mastering".into()));
        assert_eq!(second, ("project-song".into(), "daw-song".into()));
        assert!(read_shared_id(&fallback_project).is_empty());
    }

    #[test]
    fn empty_daw_session_remains_empty_for_runtime_legacy_bridge() {
        let fallback_project = Arc::new(RwLock::new(String::new()));
        let fallback_daw = Arc::new(RwLock::new(String::new()));
        let groups: IdentityGroups = Mutex::new(HashMap::new());

        let resolved = resolve_role_identity(
            &fallback_project,
            &fallback_daw,
            &groups,
            "project-legacy",
            "",
        );

        assert_eq!(resolved.0, "project-legacy");
        assert_eq!(resolved.1, "");
        assert!(read_shared_id(&fallback_daw).is_empty());
    }
}
