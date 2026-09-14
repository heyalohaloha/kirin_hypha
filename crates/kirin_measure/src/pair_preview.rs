//! Demand-only, bounded PRE preview. This is advisory; explicit pairing remains authoritative.
use super::PreTmpJson;
use std::fs;
use std::path::{Path, PathBuf};

#[path = "pair_preview_budget.rs"]
pub mod budget;
#[path = "pair_preview_root.rs"]
mod root;
use budget::{Budget, Statistics, Stop};

pub const FILTER_REVISION: u32 = 1;
pub const MAX_CANDIDATES: usize = 32;
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Scope {
    pub root: PathBuf,
    pub host: u32,
    pub lifetime: u64,
    pub project: String,
    pub session: String,
    pub post: String,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Candidate {
    pub id: String,
    pub name: Option<String>,
    pub owner: Option<String>,
}
#[derive(Debug)]
pub struct Snapshot {
    pub filter_revision: u32,
    pub scope: Scope,
    pub root_identity: Option<(u64, u64)>,
    pub candidates: Vec<Candidate>,
    pub stopped: Option<Stop>,
    pub stats: Statistics,
}
impl Snapshot {
    pub fn single_available(&self) -> Option<&Candidate> {
        if self.stopped.is_some() || self.root_identity.is_none() {
            return None;
        }
        let mut available = self
            .candidates
            .iter()
            .filter(|c| c.owner.as_ref().is_none_or(|id| *id == self.scope.post));
        let first = available.next()?;
        available.next().is_none().then_some(first)
    }
}

pub fn scan(scope: Scope, cancelled: &dyn Fn() -> bool) -> Snapshot {
    let mut budget = Budget::new(cancelled);
    let mut result = Snapshot {
        filter_revision: FILTER_REVISION,
        scope,
        root_identity: None,
        candidates: Vec::new(),
        stopped: None,
        stats: Statistics::default(),
    };
    let outcome = (|| {
        if result.scope.host == 0
            || result.scope.lifetime == 0
            || !crate::path_identity::is_path_safe_component(&result.scope.project)
            || !crate::path_identity::is_path_safe_component(&result.scope.post)
            || result.scope.session.len() >= 64
        {
            return Err(Stop::Uncertain);
        }
        result.root_identity = Some(root::identity(&result.scope.root, &mut budget)?);
        let scan_root = result.scope.root.clone();
        walk(&scan_root, 0, &mut result, &mut budget)?;
        if Some(root::identity(&result.scope.root, &mut budget)?) != result.root_identity {
            return Err(Stop::Uncertain);
        }
        budget.check()
    })();
    result.stopped = outcome.err();
    result.stats = budget.finish();
    result
}

fn walk(path: &Path, depth: u8, out: &mut Snapshot, budget: &mut Budget<'_>) -> Result<(), Stop> {
    let mut entries = budget.operation(|| fs::read_dir(path))?;
    loop {
        // At the exact bound we cannot assume EOF without visiting one extra entry.
        if budget.stats.entries == 512 {
            return Err(Stop::Limit);
        }
        let Some(entry) = budget.operation(|| entries.next().transpose())? else {
            return Ok(());
        };
        budget.entry()?;
        let kind = budget.operation(|| entry.file_type())?;
        let name = entry.file_name();
        if kind.is_symlink() {
            return Err(Stop::Uncertain);
        }
        if depth < 2 && kind.is_dir() {
            let Some(name) = name.to_str() else {
                return Err(Stop::Uncertain);
            };
            if depth == 0
                && matches!(
                    name,
                    "pair_target" | ".pair_engine_bindings" | "record_signal"
                )
            {
                continue;
            }
            if !crate::path_identity::is_path_safe_component(name) {
                continue;
            }
            walk(&entry.path(), depth + 1, out, budget)?;
        } else if depth == 2 && name == "pre.json" {
            if !kind.is_file() {
                return Err(Stop::Uncertain);
            }
            read_pre(&entry.path(), out, budget)?;
        }
    }
}

fn read_pre(path: &Path, out: &mut Snapshot, budget: &mut Budget<'_>) -> Result<(), Stop> {
    let bytes = budget.json(path)?.ok_or(Stop::Uncertain)?;
    let pre: PreTmpJson = serde_json::from_slice(&bytes).map_err(|_| Stop::Uncertain)?;
    let instance = path.parent().ok_or(Stop::Uncertain)?;
    if pre.instance_id.len() >= 64
        || !crate::path_identity::is_path_safe_component(&pre.instance_id)
        || instance.file_name().and_then(|name| name.to_str()) != Some(&pre.instance_id)
    {
        return Err(Stop::Uncertain);
    }
    // Old/ambiguous wire evidence stays in the explicit legacy menu, never a sole-PRE assertion.
    if pre.host_process_id == 0 || pre.watch_owner_id.is_empty() {
        return Err(Stop::Uncertain);
    }
    if pre.host_process_id != out.scope.host {
        return Ok(());
    }
    let marker = crate::watch_snapshot_lease::owner_marker_path(instance, &pre.watch_owner_id)
        .ok_or(Stop::Uncertain)?;
    if !budget.locked(&marker)? {
        return Ok(());
    }
    if out.candidates.len() == MAX_CANDIDATES {
        return Err(Stop::Limit);
    }
    let owner = crate::pair_claim_index::preview_owner(
        &out.scope.root,
        out.scope.host,
        &pre.instance_id,
        budget,
    )?;
    if pre.name.as_ref().is_some_and(|name| name.len() >= 64) {
        return Err(Stop::Uncertain);
    }
    // Same-host explicit identity is the existing selector's supported boundary. No name/session inference.
    if out
        .candidates
        .iter()
        .any(|candidate| candidate.id == pre.instance_id)
    {
        return Err(Stop::Uncertain);
    }
    out.candidates.push(Candidate {
        id: pre.instance_id,
        name: pre.name.filter(|name| !name.is_empty()),
        owner,
    });
    Ok(())
}

#[cfg(test)]
#[path = "pair_preview_tests.rs"]
mod tests;
